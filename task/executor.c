/* executor.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

#include "executor.h"
#include "scheduler.h"
#include "fiber.h"

#include <foundation/thread.h>
#include <foundation/atomic.h>
#include <foundation/semaphore.h>
#include <foundation/mutex.h>

#include <foundation/windows.h>
#include <foundation/posix.h>

FOUNDATION_DECLARE_THREAD_LOCAL(task_executor_t*, task_executor_current, nullptr)

extern task_executor_t*
task_executor_thread_current(void);

task_executor_t*
task_executor_thread_current(void) {
	return get_thread_task_executor_current();
}

static task_fiber_t*
task_executor_next_free_fiber(task_executor_t* executor) {
	task_fiber_t* fiber;
	if (executor->fiber_free) {
		fiber = executor->fiber_free;
		executor->fiber_free = nullptr;
		return fiber;
	}

	mutex_lock(executor->fiber_finished_lock);
	fiber = executor->fiber_finished;
	executor->fiber_finished = fiber ? fiber->fiber_next : nullptr;
	mutex_unlock(executor->fiber_finished_lock);
	if (fiber) {
		fiber->fiber_next = nullptr;
		return fiber;
	}

	return task_scheduler_next_free_fiber(executor->scheduler);
}

static void
task_executor_fiber(task_executor_t* executor, task_fiber_t* self_fiber) {
	task_scheduler_t* scheduler = executor->scheduler;

	while (atomic_load32(&scheduler->running, memory_order_acquire)) {
		uint yield_count = 0;
		uint finished_count = 0;
		for (size_t ifiber = 0; ifiber < scheduler->fiber_count; ++ifiber) {
			if (scheduler->fiber[ifiber]->state == TASK_FIBER_YIELD)
				++yield_count;
			else if (scheduler->fiber[ifiber]->state == TASK_FIBER_FINISHED)
				++finished_count;
		}

		if (!yield_count && !finished_count) {
			FOUNDATION_ASSERT(self_fiber->state != TASK_FIBER_YIELD);
		}

		if (executor->fiber_waiting_release) {
			task_fiber_t* fiber = executor->fiber_waiting_release;
			executor->fiber_waiting_release = nullptr;

			// Release the artificial count done to make sure fiber was not prematurely
			// resumed before the switch back to executor was complete
			if (atomic_decr32(fiber->waiting_counter, memory_order_relaxed) == 0) {
				// All subtasks completed while switching to the executor fiber when
				// yielding this fiber (task), switch back and continue execution
				task_fiber_t* fiber_waiting = task_scheduler_pop_fiber_waiting(scheduler, fiber->waiting_counter);
				FOUNDATION_ASSERT(fiber_waiting == fiber);
				FOUNDATION_ASSERT_MSG(!fiber_waiting->fiber_pending_finished,
				                      "Internal fiber failure, continuation fiber already has pending finished fiber");
				FOUNDATION_ASSERT_MSG(
				    fiber_waiting->state == TASK_FIBER_YIELD,
				    "Internal fiber failure, waiting fiber not in yield state when resuming in fiber");

				task_fiber_switch(self_fiber, fiber_waiting);
			}
		}

		// Grab the next pending task, either a new task or a task to resume
		task_t task;
		if (task_scheduler_next_task(scheduler, &task)) {
			if (self_fiber->fiber_pending_finished) {
				// This will be reached once a fiber has finished executing a task and run out of
				// pending task recursions - the original task fiber is put in the executor pending
				// finished and context is switched back to this fiber to clean up
				task_executor_finished_fiber(executor, self_fiber->fiber_pending_finished);
				self_fiber->fiber_pending_finished = nullptr;
			}

			// This is a new task, grab a free fiber
			task_fiber_t* fiber = task_executor_next_free_fiber(executor);
			FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_FREE,
			                      "Internal fiber failure, free fiber not in free state");
			task_fiber_initialize(fiber);

			// Switch to the task fiber to execute it
			fiber->task = task;
			fiber->state = TASK_FIBER_RUNNING;
			task_fiber_switch(self_fiber, fiber);
		} else {
			// Task queue is empty, wait for signal
			if (atomic_load32(&scheduler->running, memory_order_relaxed))
				semaphore_try_wait(&scheduler->signal, 10);
		}
	}

	if (self_fiber->fiber_pending_finished) {
		/*FOUNDATION_ASSERT_MSG(self_fiber->fiber_pending_finished->executor == executor,
		                      "Fiber internal failure, pending finished fiber has mismatching executor");*/
		task_executor_finished_fiber(executor, self_fiber->fiber_pending_finished);
		self_fiber->fiber_pending_finished = nullptr;
	}
}

static FOUNDATION_NOINLINE void STDCALL
task_executor_trampoline(long ecx, long edx, long r8, long r9, task_executor_t* executor, task_fiber_t* self_fiber) {
	FOUNDATION_UNUSED(ecx, edx, r8, r9);
	atomic_thread_fence_sequentially_consistent();

	self_fiber->state = TASK_FIBER_EXECUTOR;

	task_executor_fiber(executor, self_fiber);
	task_fiber_switch(nullptr, self_fiber->fiber_return);
}

extern void
task_fiber_initialize_for_executor_thread(task_executor_t* executor, task_fiber_t* fiber,
                                          void (*executor_function)(long, long, long, long, task_executor_t*,
                                                                    task_fiber_t*));

void*
task_executor_thread(void* arg) {
	task_executor_t* executor = arg;
	set_thread_task_executor_current(executor);

	// Grab a fiber to get a clean contained stack space
	task_fiber_t* executor_fiber = task_scheduler_next_free_fiber(executor->scheduler);
	task_fiber_initialize_for_executor_thread(executor, executor_fiber, task_executor_trampoline);

	task_fiber_t self_fiber = {0};
#if FOUNDATION_PLATFORM_WINDOWS
	CONTEXT self_context;
	NT_TIB self_tib;
	self_fiber.context = &self_context;
	self_fiber.tib = &self_tib;
#elif FOUNDATION_PLATFORM_LINUX
	ucontext_t self_context;
	self_fiber.context = &self_context;
#else
#error Not implemented
#endif

	task_fiber_initialize_from_current_thread(&self_fiber);

	task_fiber_switch(&self_fiber, executor_fiber);

	return nullptr;
}

extern void
task_executor_finished_fiber_internal(task_executor_t* executor, task_fiber_t* fiber);

void
task_executor_finished_fiber_internal(task_executor_t* executor, task_fiber_t* fiber) {
	FOUNDATION_ASSERT_MSG(fiber->stack, "Internal fiber failure, executor control fiber marked as free");
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_FINISHED,
	                      "Internal fiber failure, finished fiber not in finished state");
	fiber->state = TASK_FIBER_FREE;

	if (!executor->fiber_free) {
		executor->fiber_free = fiber;
		return;
	}

	mutex_lock(executor->fiber_finished_lock);
	fiber->fiber_next = executor->fiber_finished;
	executor->fiber_finished = fiber;
	mutex_unlock(executor->fiber_finished_lock);
}

void
task_executor_finished_fiber(task_executor_t* executor, task_fiber_t* fiber) {
	FOUNDATION_ASSERT_MSG(fiber->stack, "Internal fiber failure, executor control fiber marked as free");
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_FINISHED,
	                      "Internal fiber failure, finished fiber not in finished state");
	fiber->state = TASK_FIBER_FREE;

	mutex_lock(executor->fiber_finished_lock);
	fiber->fiber_next = executor->fiber_finished;
	executor->fiber_finished = fiber;
	mutex_unlock(executor->fiber_finished_lock);
}
