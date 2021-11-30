/* fiber.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a cross-platform library in C11 providing
 * task-based parallellism for projects based on our foundation library.
 *
 * The latest source code maintained by Mattias Jansson is always available at
 *
 * https://github.com/mjansson/task_lib
 *
 * The foundation library source code maintained by Mattias Jansson is always available at
 *
 * https://github.com/mjansson/foundation_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include "fiber.h"
#include "executor.h"
#include "scheduler.h"

#include <foundation/atomic.h>
#include <foundation/semaphore.h>

#include <foundation/windows.h>
#include <foundation/posix.h>

extern void
task_set_current(task_t* task);

//! Used for return address of executor control fiber context
static void FOUNDATION_NOINLINE
task_fiber_dummy(void) {
}

bool FOUNDATION_NOINLINE
task_fiber_initialize_from_current_thread(task_fiber_t* fiber) {
	fiber->state = TASK_FIBER_THREAD;
#if FOUNDATION_PLATFORM_WINDOWS
	
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	memcpy(fiber->tib, tib, sizeof(NT_TIB));
	fiber->stack = (void*)tib->StackLimit;
	fiber->stack_size = pointer_diff((void*)tib->StackBase, fiber->stack);

	CONTEXT* context = fiber->context;
	context->ContextFlags = CONTEXT_FULL;
	BOOL res = GetThreadContext(GetCurrentThread(), context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to get current thread context for fiber"))
		return false;
	// The stack pointer cannot be used as set by GetThreadContext, as it will be
	// captured inside the scope of the kernel DLL function. It will contain some other
	// data when actually executed. Capture the stack pointer as seen by this function
	// and simulate a immediate return by the dummy empty function (instruction pointer
	// will point to the ret instruction). It will pop the return value from the stack
	// which we have set to the address of the return address.
	context->Rsp = (DWORD64)_AddressOfReturnAddress();
	context->Rbp = 0;
	context->Rip = (DWORD64)task_fiber_dummy;
#else
#error Not implemented
#endif
	return true;
}

#if FOUNDATION_PLATFORM_WINDOWS
static FOUNDATION_NOINLINE void __stdcall task_fiber_trampoline(long ecx, long edx, long r8, long r9,
                                                                task_fiber_t* fiber) {
	FOUNDATION_ASSERT_MSG(fiber->state != TASK_FIBER_THREAD,
	                      "Internal fiber failure, executor control fiber used as task fiber");
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state");

	task_executor_t* executor = fiber->executor;
	task_scheduler_t* scheduler = executor->scheduler;
	task_fiber_t* fiber_waiting = nullptr;

	// Mark a fiber that was pending finished as actually finished (see comment
	// below about current fiber when switching to a task with a pending fiber)
	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		atomic_thread_fence_release();
		task_executor_finished_fiber(executor, fiber_finished);
	}

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state when calling task function");

	task_set_current(&fiber->task);
	fiber->task.function(&fiber->task);

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state after calling task function");

	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		atomic_thread_fence_release();
		task_executor_finished_fiber(executor, fiber_finished);
	}

	if (fiber->task.counter) {
		if (!atomic_decr32(fiber->task.counter, memory_order_relaxed)) {
			// Get the fiber waiting for this subtask counter completion
			fiber_waiting = task_scheduler_pop_fiber_waiting(scheduler, fiber->task.counter);
		}
	}

	while (atomic_load32(&scheduler->running, memory_order_relaxed)) {
		// Check if there is a previously waiting fiber that is ready to execute
		if (fiber_waiting) {
			// This fiber has now finished, but cannot be released until the new fiber is executing in
			// it's own stack or it could be prematurely reused
			FOUNDATION_ASSERT_MSG(!fiber_waiting->fiber_pending_finished,
			                      "Internal fiber failure, continuation fiber already has pending finished fiber");
			FOUNDATION_ASSERT_MSG(!fiber->fiber_pending_finished,
			                      "Internal fiber failure, finished fiber has pending finished fiber");
			FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
			                      "Internal fiber failure, running fiber not in running state");
			fiber->state = TASK_FIBER_FINISHED;
			fiber_waiting->fiber_pending_finished = fiber;

			FOUNDATION_ASSERT_MSG(fiber_waiting->state == TASK_FIBER_YIELD,
			                      "Internal fiber failure, waiting fiber not in yield state when resuming in fiber");

			// Switch to the waiting task fiber to execute it
			task_set_current(&fiber_waiting->task);

			fiber_waiting->executor = executor;
			fiber_waiting->state = TASK_FIBER_RUNNING;
			task_fiber_switch(fiber->fiber_return, fiber_waiting);

			// We will never return here since the fiber switched to will
			// switch back to the return context immediately
			FOUNDATION_ASSERT_FAIL_LOG(HASH_TASK, "Internal fiber failure, control returned to unreachable code");
		}

		// Optimization, check if we can reuse this fiber immediately without
		// switching context back to the executor task loop (tail recursion)
		task_t task;
		if (task_scheduler_next_task(scheduler, &task)) {
			FOUNDATION_ASSERT_MSG(!task.fiber, "Internal fiber failure, new task has fiber assigned");

			// This is a new task, reuse this fiber
			task_set_current(&task);
			task.fiber = fiber;
			task.function(&task);

			if (fiber->fiber_pending_finished) {
				task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
				fiber->fiber_pending_finished = nullptr;
				atomic_thread_fence_release();
				task_executor_finished_fiber(executor, fiber_finished);
			}

			if (task.counter) {
				if (!atomic_decr32(task.counter, memory_order_relaxed)) {
					// Get the fiber waiting for this subtask counter completion
					fiber_waiting = task_scheduler_pop_fiber_waiting(scheduler, task.counter);
				}
			}
		} else {
			// Break and return to executor control fiber
			break;
		}
	}

	FOUNDATION_ASSERT_MSG(!fiber->fiber_return->fiber_pending_finished,
	                      "Internal fiber failure, return context already has pending finished fiber");
	fiber->fiber_return->fiber_pending_finished = fiber;

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state");
	fiber->state = TASK_FIBER_FINISHED;

	task_set_current(nullptr);

	HANDLE thread = GetCurrentThread();
	BOOL res = SetThreadContext(thread, (CONTEXT*)fiber->fiber_return->context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to switch current fiber context in fiber return"))
		return;
}
#endif

bool FOUNDATION_NOINLINE
task_fiber_initialize(task_fiber_t* fiber) {
#if FOUNDATION_PLATFORM_WINDOWS
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	memcpy(fiber->tib, tib, sizeof(NT_TIB));
	NT_TIB* fiber_tib = fiber->tib;
	fiber_tib->FiberData = fiber;
	fiber_tib->StackLimit = fiber->stack;
	fiber_tib->StackBase = pointer_offset(fiber->stack, -(ssize_t)fiber->stack_size);

	CONTEXT* context = fiber->context;
	context->ContextFlags = CONTEXT_FULL;
	BOOL res = GetThreadContext(GetCurrentThread(), context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to get current thread context for fiber"))
		return false;

	FOUNDATION_ASSERT_MSG(fiber->state != TASK_FIBER_THREAD,
	                      "Internal fiber failure, executor control fiber used for execution of task");
	FOUNDATION_ASSERT_MSG(fiber->stack, "Internal fiber failure, fiber without a stack used");

	void* stack_pointer = fiber->stack;
	void** argument_pointer = (void**)pointer_offset(stack_pointer, -32);
	*(argument_pointer + 0) = fiber;
	*(argument_pointer + 1) = 0;
	*(argument_pointer + 2) = 0;
	*(argument_pointer + 3) = 0;
	stack_pointer = pointer_offset(argument_pointer, -40);
	void** stack_content = (void**)stack_pointer;
	*(stack_content + 0) = 0;
	*(stack_content + 1) = 0;
	*(stack_content + 2) = 0;
	*(stack_content + 3) = 0;
	*(stack_content + 4) = 0;
	context->Rsp = (DWORD64)stack_pointer;
	context->Rbp = 0;
	context->Rip = (DWORD64)task_fiber_trampoline;
	context->ContextFlags = CONTEXT_FULL;
#else
#error Not implemented
#endif
	return true;
}

extern void
task_fiber_initialize_for_executor_thread(task_executor_t* executor, task_fiber_t* fiber,
                                          void (*executor_function)(long, long, long, long, task_executor_t*,
                                                                    task_fiber_t*));

void
task_fiber_initialize_for_executor_thread(task_executor_t* executor, task_fiber_t* fiber,
                                          void (*executor_function)(long, long, long, long, task_executor_t*,
                                                                    task_fiber_t*)) {
#if FOUNDATION_PLATFORM_WINDOWS
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	memcpy(fiber->tib, tib, sizeof(NT_TIB));
	NT_TIB* fiber_tib = fiber->tib;
	fiber_tib->FiberData = fiber;
	fiber_tib->StackLimit = fiber->stack;
	fiber_tib->StackBase = pointer_offset(fiber->stack, -(ssize_t)fiber->stack_size);

	CONTEXT* context = fiber->context;
	context->ContextFlags = CONTEXT_FULL;
	BOOL res = GetThreadContext(GetCurrentThread(), context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to get current thread context for fiber"))
		return;

	FOUNDATION_ASSERT_MSG(fiber->state != TASK_FIBER_THREAD,
	                      "Internal fiber failure, executor control fiber used for execution of task");
	FOUNDATION_ASSERT_MSG(fiber->stack, "Internal fiber failure, fiber without a stack used");

	void* stack_pointer = fiber->stack;
	void** argument_pointer = (void**)pointer_offset(stack_pointer, -32);
	*(argument_pointer + 0) = executor;
	*(argument_pointer + 1) = fiber;
	*(argument_pointer + 2) = 0;
	*(argument_pointer + 3) = 0;
	stack_pointer = pointer_offset(argument_pointer, -40);
	void** stack_content = (void**)stack_pointer;
	*(stack_content + 0) = 0;
	*(stack_content + 1) = 0;
	*(stack_content + 2) = 0;
	*(stack_content + 3) = 0;
	*(stack_content + 4) = 0;
	context->Rsp = (DWORD64)stack_pointer;
	context->Rbp = 0;
	context->Rip = (DWORD64)executor_function;
	context->ContextFlags = CONTEXT_FULL;
#else
#error Not implemented
#endif
}

void FOUNDATION_NOINLINE
task_fiber_switch(task_fiber_t* from, task_fiber_t* to) {
	to->fiber_return = from;

#if FOUNDATION_PLATFORM_WINDOWS
	BOOL res;
	HANDLE thread = GetCurrentThread();
	CONTEXT* to_context = to->context;

	// Copy new thread information block
	NT_TIB* thread_tib = (NT_TIB*)NtCurrentTeb();
	NT_TIB* fiber_tib = (NT_TIB*)to->tib;
	thread_tib->StackBase = fiber_tib->StackBase;
	thread_tib->StackLimit = fiber_tib->StackLimit;

	// Switch to fiber context
	res = SetThreadContext(thread, to_context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to switch current fiber context"))
		return;
#else
#error Not implemented
#endif
}

void FOUNDATION_NOINLINE
task_fiber_yield(task_fiber_t* fiber, atomic32_t* counter) {
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING, "Yielding a non-running fiber is not allowed");
	if (fiber->state != TASK_FIBER_RUNNING)
		return;
#if FOUNDATION_PLATFORM_WINDOWS
	HANDLE thread = GetCurrentThread();
	CONTEXT* fiber_context = fiber->context;
	BOOL res = GetThreadContext(thread, fiber_context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to store current fiber context in fiber yield"))
		return;
	fiber_context->Rip = (DWORD64)task_fiber_dummy;

	task_scheduler_push_fiber_waiting_and_yield(fiber->executor->scheduler, fiber, counter);
#else
#error Not implemented
#endif
}
