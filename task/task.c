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

#if FOUNDATION_PLATFORM_POSIX
#include <signal.h>
#endif

static task_config_t task_config;
static bool task_initialized;

static void
task_module_initialize_config(const task_config_t config) {
	task_config = config;

#if FOUNDATION_PLATFORM_POSIX
	size_t min_stack_size = (size_t)MINSIGSTKSZ;
#if BUILD_DEBUG
	size_t default_stack_size = 128 * 1024;
#else
	size_t default_stack_size = 64 * 1024;
#endif
	size_t max_stack_size = 2 * 1024 * 1024;
#else
	size_t min_stack_size = 8 * 1024;
	size_t default_stack_size = 64 * 1024;
	size_t max_stack_size = 2 * 1024 * 1024;
#endif

	if (!task_config.fiber_stack_size)
		task_config.fiber_stack_size = default_stack_size;
	else if (task_config.fiber_stack_size < min_stack_size)
		task_config.fiber_stack_size = min_stack_size;
	else if (task_config.fiber_stack_size > max_stack_size)
		task_config.fiber_stack_size = max_stack_size;
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

extern task_executor_t*
task_executor_thread_current(void);

FOUNDATION_NOINLINE void
task_yield_and_wait(atomic32_t* counter) {
	if (!counter)
		return;
	task_executor_t* executor = task_executor_thread_current();
	if (executor) {
		if (atomic_incr32(counter, memory_order_relaxed) > 1)
			task_fiber_yield(executor->fiber_current, counter);
		else
			atomic_decr32(counter, memory_order_relaxed);
	} else {
		// TODO: Do a task executor step instead of yielding thread slice
		while (atomic_load32(counter, memory_order_relaxed))
			thread_yield();
	}
}
