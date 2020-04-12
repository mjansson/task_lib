/* task.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

#include <task/task.h>
#include <task/internal.h>

#include <foundation/foundation.h>

task_config_t _task_config;
static bool _task_module_initialized;

static void
task_module_initialize_config(const task_config_t config) {
	_task_config = config;
}

int
task_module_initialize(const task_config_t config) {
	if (_task_module_initialized)
		return 0;

	task_module_initialize_config(config);

	_task_module_initialized = true;

	return 0;
}

void
task_module_finalize(void) {
	_task_module_initialized = false;
}
