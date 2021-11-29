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

#include "task.h"

#include <foundation/foundation.h>

static task_config_t task_config;
static bool task_initialized;

static void
task_module_initialize_config(const task_config_t config) {
	task_config = config;

	if (!task_config.fiber_stack_size)
		task_config.fiber_stack_size = 32 * 1024;
	else if (task_config.fiber_stack_size < 4096)
		task_config.fiber_stack_size = 4096;
	else if (task_config.fiber_stack_size > (2 * 1024 * 1024))
		task_config.fiber_stack_size = 2 * 1024 * 1024;
}

int
task_module_initialize(const task_config_t config) {
	if (task_initialized)
		return 0;

	task_module_initialize_config(config);

	task_initialized = true;

	return 0;
}

bool
task_module_is_initialized(void) {
	return task_initialized;
}

task_config_t
task_module_config(void) {
	return task_config;
}

void
task_module_finalize(void) {
	task_initialized = false;
}

void
task_yield_and_wait(task_t* task, atomic32_t* counter) {
	if (!task)
		task = task_current();

	if (counter && atomic_load32(counter, memory_order_relaxed)) {
		if (task) {
			task_fiber_yield(task->fiber, counter);
		} else {
			do {
				//TODO: Do a task executor step instead of yielding thread slice
				thread_yield();
			} while (atomic_load32(counter, memory_order_relaxed));
		}
	}
}
