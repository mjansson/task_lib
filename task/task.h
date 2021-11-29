/* task.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

/*! \file task.h
    Task library entry points */

#include <foundation/platform.h>

#include <task/types.h>
#include <task/hashstrings.h>

#include <task/executor.h>
#include <task/scheduler.h>
#include <task/fiber.h>

/*! Initialize task library
\param config Task library configuration
\return 0 if success, <0 if error */
TASK_API int
task_module_initialize(const task_config_t config);

/*! Finalize task library */
TASK_API void
task_module_finalize(void);

/*! Query if task library is initialized
\return true if initialized, false if not */
TASK_API bool
task_module_is_initialized(void);

/*! Get the task library config
\return Current configuration */
TASK_API task_config_t
task_module_config(void);

/* Get task library version
\return Task library version */
TASK_API version_t
task_module_version(void);

/*! Main task synchronization point, yield execution and wait for subtasks to complete before continuing
 * \param task Current task that should wait if known, nullptr if no task or unknown
 * \param counter Subtask counter to wait on */
TASK_API void
task_yield_and_wait(task_t* task, atomic32_t* counter);

/*! Get the current task executing in this thread
* \return Task executing in this thread, null if none */
TASK_API task_t*
task_current(void);
