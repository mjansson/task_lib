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
#include <foundation/exception.h>

#include <foundation/windows.h>
#include <foundation/posix.h>

extern task_executor_t*
task_executor_thread_current(void);

extern void
task_executor_finished_fiber_internal(task_executor_t* executor, task_fiber_t* fiber);

//! Used for return address of executor control fiber context
static void FOUNDATION_NOINLINE
task_fiber_resume(void) {
}

static void FOUNDATION_NOINLINE
task_fiber_create_landing_area(void** landing_area) {
	void** return_address = _AddressOfReturnAddress();
	*landing_area = *return_address;
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
	// and simulate a immediate return by the dummy resume function (instruction pointer
	// will point to the ret instruction). It will pop the return value from the stack
	// which we have set to the address of the return address.
	context->Rsp = (DWORD64)_AddressOfReturnAddress();
	context->Rbp = 0;
	context->Rip = (DWORD64)task_fiber_resume;
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

	task_scheduler_t* scheduler = task_executor_thread_current()->scheduler;
	task_fiber_t* fiber_waiting = nullptr;

	// Mark a fiber that was pending finished as actually finished (see comment
	// below about current fiber when switching to a task with a pending fiber)
	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		atomic_thread_fence_release();
		task_executor_finished_fiber_internal(task_executor_thread_current(), fiber_finished);
	}

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state when calling task function");

	atomic32_t* counter = fiber->task.counter;
	fiber->task.function(fiber->task.context);

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state after calling task function");

	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		atomic_thread_fence_release();
		task_executor_finished_fiber_internal(task_executor_thread_current(), fiber_finished);
	}

	if (counter) {
		if (!atomic_decr32(counter, memory_order_relaxed)) {
			// Get the fiber waiting for this subtask counter completion
			fiber_waiting = task_scheduler_pop_fiber_waiting(scheduler, counter);
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
			task_fiber_switch(fiber->fiber_return, fiber_waiting);

			// We will never return here since the fiber switched to will
			// switch back to the return context immediately
			FOUNDATION_ASSERT_FAIL_LOG(HASH_TASK, "Internal fiber failure, control returned to unreachable code");
			exception_raise_abort();
		}

		// Optimization, check if we can reuse this fiber immediately without
		// switching context back to the executor task loop (tail recursion)
		if (task_scheduler_next_task(scheduler, &fiber->task)) {
			// This is a new task, reuse this fiber
			counter = fiber->task.counter;
			fiber->task.function(fiber->task.context);

			FOUNDATION_ASSERT_MSG(
			    fiber->state == TASK_FIBER_RUNNING,
			    "Internal fiber failure, running fiber not in running state after calling task function");

			if (fiber->fiber_pending_finished) {
				task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
				fiber->fiber_pending_finished = nullptr;
				atomic_thread_fence_release();
				task_executor_finished_fiber_internal(task_executor_thread_current(), fiber_finished);
			}

			if (counter) {
				if (!atomic_decr32(counter, memory_order_relaxed)) {
					// Get the fiber waiting for this subtask counter completion
					fiber_waiting = task_scheduler_pop_fiber_waiting(scheduler, counter);
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

	FOUNDATION_ASSERT_MSG(fiber->fiber_return->state == TASK_FIBER_EXECUTOR,
	                      "Internal fiber failure, return to executor fiber not in executor state");
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state");
	fiber->state = TASK_FIBER_FINISHED;

	task_fiber_switch(nullptr, fiber->fiber_return);
}
#endif

bool FOUNDATION_NOINLINE
task_fiber_initialize(task_fiber_t* fiber) {
#if FOUNDATION_PLATFORM_WINDOWS
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	memcpy(fiber->tib, tib, sizeof(NT_TIB));
	NT_TIB* fiber_tib = fiber->tib;
	// fiber_tib->FiberData = fiber;
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
	fiber->state = TASK_FIBER_EXECUTOR;
	fiber->fiber_next = nullptr;

#if FOUNDATION_PLATFORM_WINDOWS
	NT_TIB* tib = (NT_TIB*)NtCurrentTeb();
	memcpy(fiber->tib, tib, sizeof(NT_TIB));
	NT_TIB* fiber_tib = fiber->tib;
	// fiber_tib->FiberData = fiber;
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

FOUNDATION_NOINLINE void
task_fiber_switch(task_fiber_t* from, task_fiber_t* to) {
	FOUNDATION_ASSERT(to != nullptr);
	FOUNDATION_ASSERT(!to->fiber_next);
	if (from)
		to->fiber_return = from;

	task_executor_thread_current()->fiber_current = to;

#if FOUNDATION_PLATFORM_WINDOWS
	BOOL res;
	HANDLE thread = GetCurrentThread();
	CONTEXT* to_context = to->context;

	// Copy stack pointers to new thread information block
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

FOUNDATION_NOINLINE void
task_fiber_push_waiting_and_yield(volatile void* stack_reserve, task_fiber_t* fiber, atomic32_t* counter) {
	FOUNDATION_UNUSED(stack_reserve);
	task_scheduler_push_fiber_waiting_and_yield(task_executor_thread_current()->scheduler, fiber, counter);
}

FOUNDATION_NOINLINE void
task_fiber_yield(task_fiber_t* fiber, atomic32_t* counter) {
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING, "Yielding a non-running fiber is not allowed");
	if (fiber->state != TASK_FIBER_RUNNING)
		return;
#if FOUNDATION_PLATFORM_WINDOWS
	HANDLE thread = GetCurrentThread();
	CONTEXT* context = fiber->context;
	BOOL res = GetThreadContext(thread, context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to store current fiber context in fiber yield")) {
		exception_raise_abort();
		return;
	}
#else
#error Not implemented
#endif
	if (fiber->state == TASK_FIBER_RUNNING) {
		atomic_thread_fence_release();
		volatile void* stack_reserve = alloca(128);
		task_fiber_push_waiting_and_yield(stack_reserve, fiber, counter);
	}
	if (fiber->state == TASK_FIBER_YIELD) {
		fiber->state = TASK_FIBER_RUNNING;
	}
}
