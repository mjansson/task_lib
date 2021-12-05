/* fiber.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file fiber.h
    Task fiber abstraction */

#include <foundation/platform.h>

#include <task/types.h>
#include <task/hashstrings.h>

/*! Initialize a fiber for calling a 
* \param fiber Fiber control structure
* \return true if success, false if error
*/
TASK_API bool
task_fiber_initialize_from_current_thread(task_fiber_t* fiber);

/*! Initialize a fiber for executing a task
* \param fiber Fiber control structure
*/
TASK_API bool
task_fiber_initialize(task_fiber_t* fiber);

/*! Switch fiber
* \param from Fiber to switch from
* \param to Fiber to switch to
*/
TASK_API void
task_fiber_switch(task_fiber_t* from, task_fiber_t* to);

/*! Yield fiber
* \param fiber Fiber to yield
* \param counter Counter to wait on
*/
TASK_API void
task_fiber_yield(task_fiber_t* fiber, atomic32_t* counter);

/*! Get the current fiber executing in this thread
 * \return Fiber executing in this thread, null if none */
TASK_API task_fiber_t*
task_fiber_current(void);
