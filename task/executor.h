/* executor.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file executor.h
    Task executor thread */

#include <foundation/platform.h>

#include <task/types.h>
#include <task/hashstrings.h>

/*! Task executor thread entry point
 * \param arg Thread argument (executor pointer)
 * \return Result (0)
 */
TASK_API void*
task_executor_thread(void* arg);

/*! Notify executor that the fiber finished executing
 * \param executor Task executor
 * \param fiber Free fiber control structure */
TASK_API void
task_executor_finished_fiber(task_executor_t* executor, task_fiber_t* fiber);
