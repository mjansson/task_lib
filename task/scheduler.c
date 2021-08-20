/* scheduler.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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
 * This library is put in the public domain; you can redistribute it and/or modify it without any
 * restrictions.
 *
 */

#include <task/task.h>
#include <task/internal.h>

#include <foundation/foundation.h>

#define COUNTER_BITS 12
#define COUNTER_MASK ((1 << COUNTER_BITS) - 1)
#define SLOT_SHIFT COUNTER_BITS
#define SLOT_MASK ((1 << (32 - COUNTER_BITS)) - 1)

#define RESUME_TOKEN_INDETERMINATE -1
#define RESUME_TOKEN_TERMINATE -2
#define RESUME_TOKEN_LAST RESUME_TOKEN_TERMINATE

typedef tick_t (*task_scheduler_fn)(task_scheduler_t*, task_t*, void*, tick_t);

static void*
task_executor(void* arg);

static void*
task_scheduler(void* arg);

task_scheduler_t*
task_scheduler_allocate(size_t executor_count, size_t queue_size) {
	task_scheduler_t* scheduler;
	scheduler = memory_allocate(HASH_TASK, sizeof(task_scheduler_t) + sizeof(task_instance_t) * queue_size, 0,
	                            MEMORY_PERSISTENT);
	task_scheduler_initialize(scheduler, executor_count, queue_size);
	return scheduler;
}

void
task_scheduler_initialize(task_scheduler_t* scheduler, size_t executor_count, size_t queue_size) {
	size_t islot;
	memset(scheduler, 0, sizeof(task_scheduler_t));
	scheduler->slots_count = queue_size;
	for (islot = 0; islot < queue_size; ++islot)
		atomic_store32(&scheduler->slots[islot].next, (int)(islot + 1) << SLOT_SHIFT, memory_order_release);
	atomic_store32(&scheduler->slots[queue_size - 1].next, -1, memory_order_release);
	atomic_store32(&scheduler->queue, -1, memory_order_release);
	semaphore_initialize(&scheduler->signal, 0);
	task_scheduler_set_executor_count(scheduler, executor_count);
#if BUILD_TASK_ENABLE_STATISTICS
	atomic_store64(&scheduler->minimum_latency, 0xFFFFFFFFFFFFLL, memory_order_release);
	atomic_store64(&scheduler->minimum_execution, 0xFFFFFFFFFFFFLL, memory_order_release);
#endif
}

void
task_scheduler_finalize(task_scheduler_t* scheduler) {
	task_scheduler_stop(scheduler);
	semaphore_finalize(&scheduler->signal);
	for (size_t iexec = 0, esize = array_size(scheduler->executor); iexec < esize; ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		thread_finalize(&executor->thread);
		semaphore_finalize(&executor->signal);
	}
	array_deallocate(scheduler->executor);
}

void
task_scheduler_deallocate(task_scheduler_t* scheduler) {
	task_scheduler_finalize(scheduler);
	memory_deallocate(scheduler);
}

static int
_task_scheduler_queue(task_scheduler_t* scheduler, const task_t task, task_arg_t arg, tick_t when) {
	task_instance_t* instance;
	int32_t slot, rawslot, rawnext, next, counter;
	if (!when)
		when = time_current();
	// Grab a free slot
	while ((rawslot = atomic_load32(&scheduler->free, memory_order_acquire)) >= 0) {
		slot = (rawslot >> SLOT_SHIFT) & SLOT_MASK;
		instance = scheduler->slots + slot;

		rawnext = atomic_load32(&instance->next, memory_order_acquire);
		if (rawnext >= 0) {
			counter = rawnext & COUNTER_MASK;
			rawnext = (rawnext & ~COUNTER_MASK) | (++counter & COUNTER_MASK);
		}

		if (atomic_cas32(&scheduler->free, rawnext, rawslot, memory_order_release, memory_order_acquire)) {
			instance->task = task;
			instance->arg = arg;
			instance->when = when;

			// Add it to queue
			counter = rawslot & COUNTER_MASK;
			rawnext = (rawslot & ~COUNTER_MASK) | (++counter & COUNTER_MASK);
#if BUILD_TASK_ENABLE_DEBUG_LOG
			log_debugf(HASH_TASK, STRING_CONST("Queued task on slot %d (counter %d)"), slot, counter);
#endif
			do {
				next = atomic_load32(&scheduler->queue, memory_order_acquire);
				atomic_store32(&instance->next, next, memory_order_release);
			} while (!atomic_cas32(&scheduler->queue, rawnext, next, memory_order_release, memory_order_acquire));

			return next < 0 ? 1 : -1;
		}
	}

	log_errorf(HASH_TASK, ERROR_OUT_OF_MEMORY,
	           STRING_CONST("Unable to queue task to task scheduler %" PRIfixPTR ", queue full"), (uintptr_t)scheduler);
	return 0;
}

void
task_scheduler_queue(task_scheduler_t* scheduler, const task_t task, task_arg_t arg, tick_t when) {
	if (_task_scheduler_queue(scheduler, task, arg, when ? when : time_current()) > 0)
		semaphore_post(&scheduler->signal);
}

void
task_scheduler_multiqueue(task_scheduler_t* scheduler, const task_t* tasks, const task_arg_t* args, size_t tasks_count,
                          tick_t* when) {
	bool signal = false;
	tick_t curtime = 0;
	task_arg_t nullarg = 0;

	for (unsigned int it = 0; it < tasks_count; ++it) {
		const task_t* current_task = tasks++;
		const task_arg_t* current_arg = args ? args++ : nullptr;
		tick_t current_when = when ? *when++ : 0;

		if (!current_when && !curtime)
			curtime = time_current();

		int res = _task_scheduler_queue(scheduler, *current_task, current_arg ? *current_arg : nullarg,
		                                current_when ? current_when : curtime);
		if (res > 0)
			signal = true;
		else if (!res)
			break;
	}

	if (signal)
		semaphore_post(&scheduler->signal);
}

size_t
task_scheduler_executor_count(task_scheduler_t* scheduler) {
	return array_size(scheduler->executor);
}

bool
task_scheduler_set_executor_count(task_scheduler_t* scheduler, size_t executor_count) {
	if (scheduler->running)
		return false;

	// TODO: If 1 executor, merge scheduler and executor into a single wait-step executor
	size_t previous = array_size(scheduler->executor);
	memory_context_push(HASH_TASK);
	if (previous > executor_count) {
		for (size_t iexec = executor_count; iexec < previous; ++iexec) {
			task_executor_t* executor = scheduler->executor + iexec;
			thread_finalize(&executor->thread);
			semaphore_finalize(&executor->signal);
		}
		array_resize(scheduler->executor, executor_count);
	} else {
		array_resize(scheduler->executor, executor_count);
		for (size_t iexec = previous; iexec < executor_count; ++iexec) {
			task_executor_t* executor = scheduler->executor + iexec;
			memset(executor, 0, sizeof(task_executor_t));
			executor->scheduler = scheduler;
			thread_initialize(&executor->thread, task_executor, executor, STRING_CONST("task_executor"),
			                  THREAD_PRIORITY_NORMAL, 0);
			semaphore_initialize(&executor->signal, 0);
		}
	}
	memory_context_pop();

	return true;
}

static tick_t
_task_execute(task_scheduler_t* scheduler, task_t* task, void* arg, tick_t when) {
	tick_t resume = 0;
#if (BUILD_ENABLE_DEBUG_LOG && BUILD_TASK_ENABLE_DEBUG_LOG) || BUILD_TASK_ENABLE_STATISTICS
	tick_t starttime = time_current();
#if BUILD_TASK_ENABLE_STATISTICS
	tick_t maxtime, mintime;
	tick_t latency_time = starttime - when;
	atomic_add64(&scheduler->total_latency, latency_time, memory_order_release);
	mintime = atomic_load64(&scheduler->minimum_latency, memory_order_acquire);
	if (mintime > latency_time)
		atomic_cas64(&scheduler->minimum_latency, latency_time, mintime, memory_order_release, memory_order_acquire);
	maxtime = atomic_load64(&scheduler->maximum_latency, memory_order_acquire);
	if (maxtime < latency_time)
		atomic_cas64(&scheduler->maximum_latency, latency_time, maxtime, memory_order_release, memory_order_acquire);
#endif
#if BUILD_ENABLE_DEBUG_LOG && BUILD_TASK_ENABLE_DEBUG_LOG
	log_debugf(HASH_TASK, STRING_CONST("Task latency: %.5fms (%" PRIu64 " ticks) for %.*s"),
	           1000.0f * (float)time_ticks_to_seconds(starttime - when), (starttime - when), STRING_FORMAT(task->name));
#endif
#else
	FOUNDATION_UNUSED(when);
#endif

	profile_begin_block(STRING_CONST("task execute"));
	error_context_push(STRING_CONST("executing task"), STRING_ARGS(task->name));

	task_return_t ret = task->function(arg);
	if (ret.result == TASK_YIELD) {
		resume = time_current();
		if (ret.value > 0)
			resume += ret.value;
	}
	// else finished or aborted, so done

	error_context_pop();
	profile_end_block();

#if (BUILD_ENABLE_DEBUG_LOG && BUILD_TASK_ENABLE_DEBUG_LOG) || BUILD_TASK_ENABLE_STATISTICS
	tick_t endtime = time_current();
#if BUILD_TASK_ENABLE_STATISTICS
	tick_t execute_time = endtime - starttime;
	atomic_incr64(&scheduler->executed_count, memory_order_release);
	atomic_add64(&scheduler->total_execution, execute_time, memory_order_release);
	mintime = atomic_load64(&scheduler->minimum_execution, memory_order_acquire);
	if (mintime > execute_time)
		atomic_cas64(&scheduler->minimum_execution, execute_time, mintime, memory_order_release, memory_order_acquire);
	maxtime = atomic_load64(&scheduler->maximum_execution, memory_order_acquire);
	if (maxtime < execute_time)
		atomic_cas64(&scheduler->maximum_execution, execute_time, maxtime, memory_order_release, memory_order_acquire);
#endif
#if BUILD_ENABLE_DEBUG_LOG && BUILD_TASK_ENABLE_DEBUG_LOG
	log_debugf(HASH_TASK, STRING_CONST("Task execution: %.5fms (%" PRIu64 " ticks) for %.*s"),
	           1000.0f * (float)time_ticks_to_seconds(endtime - starttime), (endtime - starttime),
	           STRING_FORMAT(task->name));
#endif
#endif
	return resume;
}

static tick_t
_task_schedule(task_scheduler_t* scheduler, task_t* task, void* arg, tick_t when) {
	do {
		// TODO: Improve lookup of free executors
		for (size_t iexec = 0, esize = array_size(scheduler->executor); iexec < esize; ++iexec) {
			task_executor_t* executor = scheduler->executor + iexec;
			if (atomic_cas32(&executor->flag, 1, 0, memory_order_release, memory_order_acquire)) {
				executor->task = *task;
				executor->when = when;
				executor->arg = arg;
#if BUILD_TASK_ENABLE_DEBUG_LOG
				log_debugf(HASH_TASK, STRING_CONST("Scheduling task '%.*s' on executor %" PRIsize),
				           STRING_FORMAT(task->name), iexec);
#endif
				semaphore_post(&executor->signal);
				return RESUME_TOKEN_INDETERMINATE;
			}
		}
		thread_yield();
	} while (!thread_try_wait(0));

	return 0;
#if 0
	// No free executor, but running on scheduler thread will block further task launches
	// until task is done.
	// TODO: Allow eager task stealing from within executors to allow scheduler to grab tasks?
#if BUILD_TASK_ENABLE_DEBUG_LOG
	log_debugf(HASH_TASK, STRING_CONST("Executing task '%.*s' on scheduler thread"), STRING_FORMAT(task->name));
#endif
	tick_t resume = _task_execute(scheduler, task, arg, when);
	return (resume ? -resume : RESUME_TOKEN_TERMINATE);
#endif
}

void
task_scheduler_start(task_scheduler_t* scheduler) {
	if (scheduler->running)
		return;

	scheduler->running = true;
	log_infof(HASH_TASK, STRING_CONST("Starting task scheduler 0x%" PRIfixPTR " with %" PRIsize " executor threads"),
	          (uintptr_t)scheduler, task_scheduler_executor_count(scheduler));

	for (size_t iexec = 0, esize = array_size(scheduler->executor); iexec < esize; ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		thread_start(&executor->thread);
	}

	thread_initialize(&scheduler->thread, task_scheduler, scheduler, STRING_CONST("task_scheduler"),
	                  THREAD_PRIORITY_NORMAL, 0);
	thread_start(&scheduler->thread);
}

void
task_scheduler_stop(task_scheduler_t* scheduler) {
	if (!scheduler->running)
		return;

	log_infof(HASH_TASK, STRING_CONST("Terminating task scheduler 0x%" PRIfixPTR " with %" PRIsize " executor threads"),
	          (uintptr_t)scheduler, task_scheduler_executor_count(scheduler));

	thread_signal(&scheduler->thread);
	semaphore_post(&scheduler->signal);
	thread_finalize(&scheduler->thread);

	for (size_t iexec = 0, execsize = array_size(scheduler->executor); iexec < execsize; ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		thread_signal(&executor->thread);
		semaphore_post(&executor->signal);
	}

	for (size_t iexec = 0, execsize = array_size(scheduler->executor); iexec < execsize; ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		thread_join(&executor->thread);
	}

	scheduler->running = false;
}

static tick_t
_task_scheduler_step(task_scheduler_t* scheduler, int limit_ms, task_scheduler_fn execute_fn) {
	tick_t enter_time, limit_time;
	tick_t next_task_time = 0;
	int32_t raw_slot, slot, counter;
	int32_t remain, last_remain_slot;
	int32_t free, last_free_slot;

	atomic_thread_fence_acquire();
	if (atomic_load32(&scheduler->queue, memory_order_acquire) < 0)
		return 0;

	profile_begin_block(STRING_CONST("task step"));

	enter_time = time_current();
	limit_time = 0;

	do {
		raw_slot = atomic_load32(&scheduler->queue, memory_order_acquire);
	} while (!atomic_cas32(&scheduler->queue, -1, raw_slot, memory_order_release, memory_order_acquire));

	free = last_free_slot = -1;
	remain = last_remain_slot = -1;

	counter = (raw_slot & COUNTER_MASK);
	slot = (raw_slot >= 0) ? (raw_slot >> SLOT_SHIFT) & SLOT_MASK : -1;

	while (slot >= 0) {
		task_instance_t* instance = &scheduler->slots[slot];
		int32_t raw_next = atomic_load32(&instance->next, memory_order_acquire);
		int32_t counter_next = (raw_next & COUNTER_MASK);
		int32_t slot_next = (raw_next >= 0) ? (raw_next >> SLOT_SHIFT) & SLOT_MASK : -1;
		tick_t resume;
		bool endloop = false;
		bool executed = false;

		if (!instance->when || (instance->when <= enter_time)) {
			resume = execute_fn(scheduler, &instance->task, instance->arg, instance->when);
			executed = true;
			endloop = (resume < RESUME_TOKEN_INDETERMINATE);
			if (resume < RESUME_TOKEN_LAST)
				resume = -resume;
		} else {
			resume = instance->when;
		}

		if (resume <= 0) {
			// Task executed or aborted, free up slot
			atomic_store32(&scheduler->slots[slot].next, free, memory_order_release);
			free = (slot << SLOT_SHIFT) | (++counter & COUNTER_MASK);

			if (last_free_slot == -1)
				last_free_slot = slot;
		} else {
			// waiting or yielded, keep in queue
			if (!next_task_time || (resume < next_task_time))
				next_task_time = resume;

			atomic_store32(&scheduler->slots[slot].next, remain, memory_order_release);
			remain = (slot << SLOT_SHIFT) | (++counter & COUNTER_MASK);

			if (last_remain_slot == -1)
				last_remain_slot = slot;
		}

		slot = slot_next;
		counter = counter_next;

		if (endloop || (!limit_ms && executed))
			break;
		if (limit_ms > 0) {
			if (!limit_time)
				limit_time = (limit_ms * time_ticks_per_second()) / 1000LL;
			if (time_elapsed_ticks(enter_time) >= limit_time)
				break;
		}
	}

	if (slot >= 0) {
		// Slot is now first task that is not executed (execution timeboxed)
		// Add yielded tasks at end of this sub-queue, in order to get round-robin
		// behaviour of task execution
		int32_t raw_next;
		int32_t last_slot = slot;
		int32_t slot_next = slot;
		while (slot_next >= 0) {
			last_slot = slot_next;
			if (!next_task_time || (scheduler->slots[last_slot].when < next_task_time))
				next_task_time = scheduler->slots[last_slot].when;
			raw_next = atomic_load32(&scheduler->slots[last_slot].next, memory_order_acquire);
			slot_next = (raw_next >= 0) ? (raw_next >> SLOT_SHIFT) & SLOT_MASK : -1;
		}
		if (remain >= 0) {
			atomic_store32(&scheduler->slots[last_slot].next, remain, memory_order_release);
		} else {
			remain = (slot << SLOT_SHIFT) | (++counter & COUNTER_MASK);
			last_remain_slot = last_slot;
		}
	}

	// Reinsert non-executed/yielded tasks at front of queue (in front of any tasks
	// that were queued during execution)
	if (remain >= 0) {
		do {
			raw_slot = atomic_load32(&scheduler->queue, memory_order_acquire);
			counter = raw_slot & COUNTER_MASK;
			slot = (raw_slot >= 0) ? (raw_slot & ~COUNTER_MASK) | (++counter & COUNTER_MASK) : -1;
			atomic_store32(&scheduler->slots[last_remain_slot].next, slot, memory_order_release);
		} while (!atomic_cas32(&scheduler->queue, remain, raw_slot, memory_order_release, memory_order_acquire));
		if (raw_slot >= 0)
			next_task_time = -1;  // Queue changed, no prediction of next task time
	}

	// Reinsert completed tasks as free
	if (free >= 0) {
		do {
			// Append current free slots at end of new free list
			raw_slot = atomic_load32(&scheduler->free, memory_order_acquire);
			counter = raw_slot & COUNTER_MASK;
			slot = (raw_slot >= 0) ? (raw_slot & ~COUNTER_MASK) | (++counter & COUNTER_MASK) : -1;
			atomic_store32(&scheduler->slots[last_free_slot].next, slot, memory_order_release);
		} while (!atomic_cas32(&scheduler->free, free, raw_slot, memory_order_release, memory_order_acquire));
	}

	profile_end_block();

	if (!next_task_time)
		return (atomic_load32(&scheduler->queue, memory_order_acquire) == -1) ? 0 : -1;

	return next_task_time;
}

tick_t
task_scheduler_step(task_scheduler_t* scheduler, int limit_ms) {
	return _task_scheduler_step(scheduler, limit_ms, _task_execute);
}

static void*
task_executor(void* arg) {
	task_executor_t* executor = arg;

	do {
		semaphore_wait(&executor->signal);
		if (atomic_cas32(&executor->flag, 2, 1, memory_order_release, memory_order_acquire)) {
			tick_t resume = _task_execute(executor->scheduler, &executor->task, executor->arg, executor->when);
			if (resume > 0)
				task_scheduler_queue(executor->scheduler, executor->task, executor->arg, resume);
			atomic_cas32(&executor->flag, 0, 2, memory_order_release, memory_order_acquire);
		}
	} while (!thread_try_wait(0));

	return 0;
}

static void*
task_scheduler(void* arg) {
	task_scheduler_t* scheduler = arg;
	tick_t ticks_per_second = time_ticks_per_second();
	bool has_executors = (array_size(scheduler->executor) > 0);
	task_scheduler_fn execute_fn = has_executors ? _task_schedule : _task_execute;

	while (!thread_try_wait(0)) {
		tick_t next_task_time = _task_scheduler_step(scheduler, -1, execute_fn);
		if (!next_task_time) {
			scheduler->idle = true;
			semaphore_wait(&scheduler->signal);
			scheduler->idle = false;
			atomic_thread_fence_release();
		} else if (next_task_time > 0) {
			tick_t curtime = time_current();
			if (next_task_time > curtime) {
				tick_t wait = ((next_task_time - curtime) * 1000) / ticks_per_second;
				if (wait)
					semaphore_try_wait(&scheduler->signal, (unsigned int)wait);
			}
		}
	}

	return 0;
}

bool
task_scheduler_is_idle(task_scheduler_t* scheduler) {
	atomic_thread_fence_acquire();
	return scheduler->idle;
}

task_statistics_t
task_scheduler_statistics(task_scheduler_t* scheduler) {
	task_statistics_t stats;
	memset(&stats, 0, sizeof(stats));
#if BUILD_TASK_ENABLE_STATISTICS
	stats.executed_count = (size_t)atomic_load64(&scheduler->executed_count, memory_order_acquire);
	if (stats.executed_count) {
		const real mult = REAL_C(1000.0);
		tick_t tps = time_ticks_per_second();
		int64_t executed_count = atomic_load64(&scheduler->executed_count, memory_order_acquire);
		int64_t total_latency = atomic_load64(&scheduler->total_latency, memory_order_acquire);
		int64_t maximum_latency = atomic_load64(&scheduler->maximum_latency, memory_order_acquire);
		int64_t minimum_latency = atomic_load64(&scheduler->minimum_latency, memory_order_acquire);
		int64_t total_exec = atomic_load64(&scheduler->total_execution, memory_order_acquire);
		int64_t maximum_exec = atomic_load64(&scheduler->maximum_execution, memory_order_acquire);
		int64_t minimum_exec = atomic_load64(&scheduler->minimum_execution, memory_order_acquire);
		stats.average_latency = mult * (real)((double)total_latency / (double)(executed_count * tps));
		stats.maximum_latency = mult * (real)((double)maximum_latency / (double)tps);
		stats.minimum_latency = mult * (real)((double)minimum_latency / (double)tps);
		stats.average_execution = mult * (real)((double)total_exec / (double)(executed_count * tps));
		stats.maximum_execution = mult * (real)((double)maximum_exec / (double)tps);
		stats.minimum_execution = mult * (real)((double)minimum_exec / (double)tps);
	}
#endif
	return stats;
}
