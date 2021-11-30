/* scheduler.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

#pragma once

/*! \file scheduler.h
    Task scheduler */

#include <foundation/platform.h>

#include <task/types.h>
#include <task/hashstrings.h>

/*! Allocate a scheduler
\param executor_count Number of executor threads
\param fiber_count Number of fibers
\return New task scheduler */
TASK_API task_scheduler_t*
task_scheduler_allocate(size_t executor_count, size_t fiber_count);

/*! Deallocate a scheduler
\param scheduler Task scheduler */
TASK_API void
task_scheduler_deallocate(task_scheduler_t* scheduler);

/*! Queue a task
\param scheduler Task scheduler
\param task Task */
TASK_API void
task_scheduler_queue(task_scheduler_t* scheduler, task_t task);

/*! Queue multiple tasks
\param scheduler Task scheduler
\param task Tasks
\param task_count Number of tasks */
TASK_API void
task_scheduler_multiqueue(task_scheduler_t* scheduler, task_t* task, size_t task_count);

/*! Pop the next task from the scheduler queue
 * \param scheduler Task scheduler
 * \param task Task structure to fill */
TASK_API bool
task_scheduler_next_task(task_scheduler_t* scheduler, task_t* task);

/*! Pop a free fiber from the scheduler pool
 * \param scheduler Task scheduler
 * \return Free fiber control structure */
TASK_API task_fiber_t*
task_scheduler_next_free_fiber(task_scheduler_t* scheduler);

/*! Add fiber as waiting on subtask counter
 * \param scheduler Scheduler
 * \param fiber Fiber
 * \param counter Subtask counter
 * \return true if fiber is ready to execute, false if waiting on subtask counter */
TASK_API bool
task_scheduler_push_fiber_waiting_and_yield(task_scheduler_t* scheduler, task_fiber_t* fiber, atomic32_t* counter);

/*! Get fiber that was waiting for the given counter
 * \param scheduler Scheduler
 * \param counter Subtask counter
 * \return Fiber that was waiting for the given subtask counter */
TASK_API task_fiber_t*
task_scheduler_pop_fiber_waiting(task_scheduler_t* scheduler, atomic32_t* counter);
