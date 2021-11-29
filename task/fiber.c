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

//! Used for return address of executor control fiber context
static void FOUNDATION_NOINLINE
task_fiber_dummy(void) {
}

bool FOUNDATION_NOINLINE
task_fiber_initialize_from_current_thread(task_fiber_t* fiber) {
	fiber->state = TASK_FIBER_THREAD;
#if FOUNDATION_PLATFORM_WINDOWS
	CONTEXT* context = fiber->context;
	context->ContextFlags = CONTEXT_FULL;
	BOOL res = GetThreadContext(GetCurrentThread(), context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to get current thread context for fiber"))
		return false;
	context->Rip = (DWORD64)task_fiber_dummy;
#else
#error Not implemented
#endif
	return true;
}

#if FOUNDATION_PLATFORM_WINDOWS
static FOUNDATION_NOINLINE void __stdcall task_fiber_jump(long ecx, long edx, long r8, long r9, task_fiber_t* fiber) {
	FOUNDATION_ASSERT_MSG(fiber->stack_size, "Internal fiber failure, executor control fiber used as task fiber");
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state");

	// Mark a fiber that was pending finished as actually finished (see comment
	// below about current fiber when switching to a task with a pending fiber)
	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		atomic_thread_fence_release();
		task_executor_finished_fiber(fiber->executor, fiber_finished);
	}

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                "Internal fiber failure, running fiber not in running state when calling task function");

	fiber->task.function(&fiber->task);

	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, running fiber not in running state after calling task function");

	task_executor_t* executor = fiber->executor;
	task_scheduler_t* scheduler = executor->scheduler;
	task_fiber_t* fiber_waiting = nullptr;

	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_finished = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		atomic_thread_fence_release();
		task_executor_finished_fiber(fiber->executor, fiber_finished);
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
			                      "Internal fiber failure, continuation context already has pending finished fiber");
			FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
			                      "Internal fiber failure, running fiber not in running state");
			fiber->state = TASK_FIBER_FINISHED;
			fiber_waiting->fiber_pending_finished = fiber;

			FOUNDATION_ASSERT_MSG(fiber_waiting->state == TASK_FIBER_YIELD,
			                      "Internal fiber failure, waiting fiber not in yield state when resuming in fiber");

			// Switch to the waiting task fiber to execute it
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
			FOUNDATION_ASSERT(!task.fiber);

			// This is a new task, reuse this fiber
			task.fiber = fiber;
			task.function(&task);
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

	HANDLE thread = GetCurrentThread();
	BOOL res = SetThreadContext(thread, (CONTEXT*)fiber->fiber_return->context);
	FOUNDATION_ASSERT_MSG(res != 0, "Failed to switch current fiber context in fiber return");
}
#endif

bool FOUNDATION_NOINLINE
task_fiber_initialize(task_fiber_t* fiber) {
#if FOUNDATION_PLATFORM_WINDOWS
	CONTEXT* context = fiber->context;
	context->ContextFlags = CONTEXT_FULL;
	BOOL res = GetThreadContext(GetCurrentThread(), context);
	if (!FOUNDATION_VALIDATE_MSG(res != 0, "Failed to get current thread context for fiber"))
		return false;

	FOUNDATION_ASSERT_MSG(fiber->stack, "Internal fiber failure, executor control fiber used for execution of task");

	void* stack_pointer = fiber->stack;
	void** fiber_pointer = pointer_offset(stack_pointer, -16);
	*fiber_pointer = fiber;
	stack_pointer = pointer_offset(fiber_pointer, -40);
	context->Rsp = (DWORD64)stack_pointer;
	context->Rip = (DWORD64)task_fiber_jump;
	context->ContextFlags = CONTEXT_FULL;
#else
#error Not implemented
#endif
	return true;
}

void FOUNDATION_NOINLINE
task_fiber_switch(task_fiber_t* from, task_fiber_t* to) {
	to->fiber_return = from;

#if FOUNDATION_PLATFORM_WINDOWS
	BOOL res;
	HANDLE thread = GetCurrentThread();
	CONTEXT* to_context = to->context;
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
