/* scheduler.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

#pragma once

/*! \file scheduler.h
    Task scheduler */

#include <foundation/platform.h>

#include <task/types.h>
#include <task/hashstrings.h>

/*! Allocate a scheduler
\param num_executors Number of executor threads
\param queue_size Size of task queue, 0 for default
\return New task scheduler */
TASK_API task_scheduler_t*
task_scheduler_allocate(size_t num_executors, size_t queue_size);

/*! Initialize a scheduler
\param scheduler Task scheduler
\param num_executors Number of executor threads
\param queue_size Size of task queue */
TASK_API void
task_scheduler_initialize(task_scheduler_t* scheduler, size_t num_executors, size_t queue_size);

/*! Finalize a scheduler
\param scheduler Task scheduler */
TASK_API void
task_scheduler_finalize(task_scheduler_t* scheduler);

/*! Deallocate a scheduler
\param scheduler Task scheduler */
TASK_API void
task_scheduler_deallocate(task_scheduler_t* scheduler);

/*! Queue a task
\param scheduler Task scheduler
\param task Task
\param arg Argument passed to task
\param when Timestamp when to execute task, 0 for immediate execution */
TASK_API void
task_scheduler_queue(task_scheduler_t* scheduler, const task_t task,
                     task_arg_t arg, tick_t when);

/*! Queue multiple task
\param scheduler Task scheduler
\param num Number of tasks
\param tasks Tasks
\param args Arguments passed to tasks
\param when Timestamps when to execute each tasks (0 individual value for immediate
            execution of invividual task, null for immediate execution of all tasks) */
TASK_API void
task_scheduler_multiqueue(task_scheduler_t* scheduler, size_t num, const task_t* tasks,
                          const task_arg_t* args, tick_t* when);

/*! Query numbe of task executors
\param scheduler Task scheduler
\return Number of task executor threads */
TASK_API size_t
task_scheduler_executor_count(task_scheduler_t* scheduler);

/*! Set number task executors. The scheduler must be stopped before
changing the executor count.
\param scheduler Task scheduler
\param num Number of executor threads
\return true if successful, false if error (scheduler running) */
TASK_API bool
task_scheduler_set_executor_count(task_scheduler_t* scheduler, size_t num);

/*! Start task scheduler and executor threads
\param scheduler Task scheduler */
TASK_API void
task_scheduler_start(task_scheduler_t* scheduler);

/*! Stop task scheduler and executor threads
\param scheduler Task scheduler */
TASK_API void
task_scheduler_stop(task_scheduler_t* scheduler);

/*! Step tasks in-thread
\param scheduler Task scheduler
\param milliseconds Execution time limit in milliseconds. A negative argument will
                    execute all pending tasks, a zero argument executes a single task
\return Timestamp for next task, 0 if no pending tasks remaining
        or <0 if next time is indetermined */
TASK_API tick_t
task_scheduler_step(task_scheduler_t* scheduler, int milliseconds);

/*! Query if scheduler is idle
\param scheduler Task scheduler
\return true if idle, false if not */
TASK_API bool
task_scheduler_is_idle(task_scheduler_t* scheduler);

/*! Query scheduler statistics
\param scheduler Task scheduler
\return Scheduler statistics (zero value if statistics not enabled) */
TASK_API task_statistics_t
task_scheduler_statistics(task_scheduler_t* scheduler);
