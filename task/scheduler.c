/* scheduler.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
 *
 * This library provides a cross-platform library in C11 providing
 * task-based parallellism for projects based on our foundation library.
 *
 * The latest source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels/task_lib
 *
 * The foundation library source code maintained by Rampant Pixels is always available at
 *
 * https://github.com/rampantpixels/foundation_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 *
 */

#include <task/task.h>
#include <task/internal.h>

#include <foundation/foundation.h>

#define COUNTER_BITS 12
#define COUNTER_MASK ((1 << COUNTER_BITS) - 1)
#define SLOT_SHIFT COUNTER_BITS
#define SLOT_MASK ~((1 << SLOT_SHIFT) - 1)

static void*
task_executor(object_t thread, void* arg);

static void*
task_scheduler(object_t thread, void* arg);

task_scheduler_t*
task_scheduler_allocate(size_t num_executors, size_t queue_size) {
	task_scheduler_t* scheduler;
	scheduler = memory_allocate(HASH_TASK,
	                            sizeof(task_scheduler_t) + sizeof(task_instance_t) * queue_size,
	                            0, MEMORY_PERSISTENT);
	task_scheduler_initialize(scheduler, num_executors, queue_size);
	return scheduler;
}

void
task_scheduler_initialize(size_t num_executors, size_t queue_size) {
	memset(scheduler, 0, sizeof(task_scheduler_t));
	scheduler->num_slots = queue_size;
	for (int islot = 0; islot < queue_size; ++islot)
		scheduler->slots[islot].next = islot + 1;
	scheduler->slots[queue_size - 1].next = -1;
	atomic_store32(&scheduler->queue, -1);
	return scheduler;
}

void
task_scheduler_finalize(task_scheduler_t* scheduler) {
	task_scheduler_stop(scheduler);
}

void
task_scheduler_deallocate(task_scheduler_t* scheduler) {
	task_scheduler_finalize(scheduler);
	memory_deallocate(scheduler);
}

static bool
_task_scheduler_queue(task_scheduler_t* scheduler, const task_t task, task_arg_t arg, tick_t when) {
	task_instance_t* instance;
	int32_t slot, rawslot, rawnext, next, rawqueue, queue, counter, nextslot, nextqueue;
	do {
		//Grab a free slot
		rawslot = atomic_load32(&scheduler->free);
		if (rawslot >= 0) {
			slot = (rawslot >> SLOT_SHIFT) & SLOT_MASK;
			instance = scheduler->slots + slot;

			rawnext = instance->next;
			if (rawnext >= 0) {
				counter = rawnext & COUNTER_MASK;
				rawnext = (rawnext & ~COUNTER_MASK) | (++counter & COUNTER_MASK);
			}

			if (atomic_cas32(&scheduler->free, rawnext, rawslot)) {
				instance->task = task;
				instance->arg = arg;
				instance->when = when;

				//Add it to queue
				counter = rawslot & COUNTER_MASK;
				rawnext = (rawslot & ~COUNTER_MASK) | (++counter & COUNTER_MASK);
				do {
					instance->next = atomic_load32(&scheduler->queue);
				}
				while (!atomic_cas32(&scheduler->queue, rawnext, instance->next));

				return true;
			}
		}
	}
	while (rawslot >= 0);

	log_errorf(HASH_TASK, ERROR_OUT_OF_MEMORY,
	           "Unable to queue task to task scheduler %" PRIfixPTR ", queue full", scheduler);
	return false;
}

void
task_scheduler_queue(task_scheduler_t* scheduler, const object_t task, task_arg_t arg,
                     tick_t when) {
	if (_task_scheduler_queue(scheduler, task, arg, when ? when : time_current()))
		semaphore_post(&scheduler->signal);
}


void
task_scheduler_multiqueue(task_scheduler_t* scheduler, unsigned int num, const object_t* tasks,
                          const task_arg_t* args, tick_t* when) {
	bool added = false;
	tick_t curtime = 0;

	for (unsigned int it = 0; it < num; ++it) {
		object_t current_task = *tasks++;
		task_arg_t* current_arg = args ? *args++ : 0;
		tick_t current_when = when ? *when++ : 0;

		if (!current_when && !curtime)
			curtime = time_current();

		if (_task_scheduler_queue(scheduler, current_task, current_arg,
		                          current_when ? current_when : curtime))
			added = true;
		else
			break;
	}

	if (added)
		semaphore_post(&scheduler->signal);
}

size_t
task_scheduler_executor_count(task_scheduler_t* scheduler) {
	return array_size(scheduler->executor);
}

bool
task_scheduler_set_executor_count(task_scheduler_t* scheduler, unsigned int num) {
	if (scheduler->running)
		return false;;

	//TODO: If 1 executor, merge scheduler and executor into a single wait-step executor
	memory_context_push(HASH_TASK);
	if (previous > num) {
		for (unsigned int iexec = num; iexec < previous; ++iexec) {
			thread_finalize(&executor->thread);
			semaphore_finalize(&executor->signal);
		}
		array_resize(scheduler->executor, num);
	}
	else {
		array_resize(scheduler->executor, num);
		for (unsigned int iexec = previous; iexec < num; ++iexec) {
			task_executor_t* executor = scheduler->executor + iexec;
			executor->scheduler = scheduler;
			executor->task.function = 0;
			thread_initialize(&executor->thread, task_executor, "task_executor", THREAD_PRIORITY_NORMAL, 0);
			semaphore_initialize(&executor->signal, 0);
		}
	}
	memory_context_pop();
}

static void
_task_execute(task_scheduler_t* scheduler, task_t* task, void* arg, tick_t when) {
	tick_t starttime = time_current();
	log_debugf(HASH_TASK, "Task latency: %.5fms (%lld ticks) for %s",
	           1000.0f * (float)time_ticks_to_seconds(starttime - when), (starttime - when),
	           task->name ? task->name : "<unknown>");

	error_context_push("executing task", task->name ? task->name : "<unknown>");

	task_return_t ret = task->function(task->object, arg);
	if (ret.result == TASK_YIELD) {
		tick_t resume = time_current();
		task_scheduler_queue(scheduler, task->id, arg, (ret.value > 0) ? (resume + ret.value) : resume);
	}
	//else finished or aborted, so done

	error_context_pop();

#if BUILD_ENABLE_LOG_DEBUG
	tick_t endtime = time_current();
	log_debugf(HASH_TASK, "Task execution: %.5fms (%lld ticks) for %s",
	           1000.0f * (float)time_ticks_to_seconds(endtime - starttime), (endtime - starttime),
	           task->name ? task->name : "<unknown>");
#endif
}

void
task_scheduler_start(task_scheduler_t* scheduler) {
	if (scheduler->running)
		return;

	scheduler->running = true;
	log_infof(HASH_TASK, "Starting task scheduler 0x%" PRIfixPTR " with %d executor threads", scheduler,
	          task_scheduler_executor_count(scheduler));

	for (unsigned int iexec = 0, esize = array_size(scheduler->executor); iexec < esize; ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		thread_start(&executor->thread);
	}

	thread_initialize(&scheduler->master, task_scheduler, "task_scheduler", THREAD_PRIORITY_NORMAL, 0);
	thread_start(&scheduler->master, scheduler);
}

void 
task_scheduler_stop(task_scheduler_t* scheduler) {
	if (!scheduler->running)
		return;

	log_infof(HASH_TASK, "Terminating task scheduler 0x%" PRIfixPTR " with %d executor threads",
	          scheduler, task_scheduler_executor_count(scheduler));

	thread_finalize(&scheduler->master);
	semaphore_post(&scheduler->master_signal);
	thread_destroy(scheduler->master_thread);

	for (unsigned int iexec = 0, execsize = array_size(scheduler->executor); iexec < execsize;
	        ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		thread_terminate(executor->thread);
		semaphore_post(&executor->signal);
		thread_destroy(executor->thread);
	}

	for (unsigned int iexec = 0, execsize = array_size(scheduler->executor); iexec < execsize;
	        ++iexec) {
		task_executor_t* executor = scheduler->executor + iexec;
		while (thread_is_running(executor->thread))
			thread_yield();
		executor->thread = 0;
	}

	while (thread_is_running(scheduler->master_thread))
		thread_yield();

	scheduler->running = false;
}


void task_scheduler_step(task_scheduler_t* scheduler, unsigned int limit_ms) {
	tick_t enter_time, current_time;
	int32_t pending, slot, last, next, free, last_free;

	atomic_thread_fence_acquire();
	if (!atomic_load32(&scheduler->queue))
		return;

	profile_begin_block("task step");

	enter_time = current_time = time_current();

	do {
		pending = scheduler->queue;
	}
	while (!atomic_cas32(&scheduler->queue, 0, pending));

	last = free = last_free = -1;

	if (pending > 0) do {
			bool waiting = false;
			task_instance_t instance = scheduler->slots[slot];
			object_t id = instance.task;
			next = instance.next;

			if (!instance.when || (instance.when <= current_time)) {
				_task_execute(scheduler, task, instance.arg, instance.when);
			}
			else {
				//waiting, keep in queue
				waiting = true;
			}

			if (!waiting) {
				if (pending == slot)
					pending = next;
				else
					scheduler->slots[last].next = next;
				scheduler->slots[slot].next = free;
				free = slot;
				if (last_free == -1)
					last_free = slot;
			}
			else {
				last = slot;
			}

			slot = next;
		}
		while ((time_elapsed(enter_time) * 1000.0 < limit_ms) &&
		        (slot >= 0));        //limit_ms=0 executes one single task

	//Reinsert completed tasks as free
	if (free >= 0) do {
			slot = scheduler->free;
			scheduler->slots[last_free].next = slot;
		}
		while (!atomic_cas32(&scheduler->free, free, slot));

	//Reinsert non-executed/yielded tasks
	if (last >= 0) do {
			slot = scheduler->queue;
			scheduler->slots[last].next = slot;
		}
		while (!atomic_cas32(&scheduler->queue, pending, slot));

	profile_end_block();
}


static void* task_executor(object_t thread, void* arg) {
	task_executor_t* executor = arg;

	do {
		semaphore_wait(&executor->signal);

		if (executor->task.function)
			_task_execute(executor->scheduler, executor->task, executor->arg, executor->when);

	} while(executor->task.function);

	return 0;
}


static void* task_scheduler(object_t thread, void* arg) {
	task_scheduler_t* scheduler = arg;

	while (!thread_should_terminate(thread)) {
		//Start queued tasks
		tick_t current_time;
		tick_t next_task_time = 0;
		int32_t pending, slot, last, next, free, last_free;

		profile_begin_block("task schedule");

		current_time = time_current();

		do {
			pending = atomic_load32(&scheduler->queue);
		}
		while (!atomic_cas32(&scheduler->queue, -1, pending));

		last = free = last_free = -1;
		slot = pending;
		aba protection in low 12 bits

		if (pending >= 0) do {
				bool done = false;
				task_instance_t instance = scheduler->slots[slot];
				object_t id = instance.task;
				next = instance.next;

				if (!instance.when || (instance.when <= current_time)) {
					task_t* task = objectmap_lookup(_task_map, id);
					if (task) {
						for (unsigned int executor_slot = 0, executor_count = task_scheduler_executor_count(scheduler);
						        !done && (executor_slot < executor_count); ++executor_slot) {
							task_executor_t* executor = scheduler->executor + executor_slot;
							if (!executor->task) {
								executor->arg = instance.arg;
								executor->task = id;
								executor->when = instance.when;
								semaphore_post(&executor->signal);
								done = true;
							}
						}

						if (!done)
							thread_yield(); //unable to find free executor, yield timeslice
					}
					else {
						//invalid, remove from queue
						log_warnf(HASH_TASK, WARNING_BAD_DATA,
						          "Task scheduler 0x%" PRIfixPTR " abandoning dangling task %llx in slot %d", scheduler, id, slot);
						task_free(id);
						done = true;
					}
				}
				else {
					//waiting, keep in queue
					if (!next_task_time || (instance.when < next_task_time))
						next_task_time = instance.when;
				}

				if (done) {
					if (pending == slot)
						pending = next;
					else
						scheduler->slots[last].next = next;
					scheduler->slots[slot].next = free;
					free = slot;
					if (last_free == -1)
						last_free = slot;
				}
				else {
					last = slot;
				}

				slot = next;
			}
			while (slot >= 0);  //limit_ms=0 executes one single task

		//Reinsert completed tasks as free
		if (free >= 0) do {
				slot = scheduler->free;
				scheduler->slots[last_free].next = slot;
			}
			while (!atomic_cas32(&scheduler->free, free, slot));

		//Reinsert non-executed/yielded tasks
		if (last >= 0) do {
				slot = scheduler->queue;
				scheduler->slots[last].next = slot;
			}
			while (!atomic_cas32(&scheduler->queue, pending, slot));

		profile_end_block();

		if (!next_task_time)
			semaphore_wait(&scheduler->master_signal);
		else
			semaphore_try_wait(&scheduler->master_signal,
			                   (int)(REAL_C(1000.0) * time_ticks_to_seconds(next_task_time - time_current())));
	}

	return 0;
}

