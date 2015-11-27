/* types.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file types.h
    Task data types */

#include <foundation/platform.h>
#include <foundation/types.h>

#include <task/build.h>

#if defined( TASK_COMPILE ) && TASK_COMPILE
#  ifdef __cplusplus
#  define TASK_EXTERN extern "C"
#  define TASK_API extern "C"
#  else
#  define TASK_EXTERN extern
#  define TASK_API extern
#  endif
#else
#  ifdef __cplusplus
#  define TASK_EXTERN extern "C"
#  define TASK_API extern "C"
#  else
#  define TASK_EXTERN extern
#  define TASK_API extern
#  endif
#endif

typedef enum task_result_t {
	TASK_FINISH = 0,
	TASK_YIELD,
	TASK_ABORT,
} task_result_t;

typedef struct task_config_t task_config_t;
typedef struct task_return_t task_return_t;
typedef struct task_t task_t;
typedef struct task_scheduler_t task_scheduler_t;
typedef struct task_executor_t task_executor_t;
typedef struct task_instance_t task_instance_t;

typedef void* task_arg_t;

typedef task_return_t (* task_fn)(task_arg_t arg);

struct task_return_t {
	task_result_t result;
	int           value;
};

struct task_t {
	task_fn        function;
	object_t       object;
	string_const_t name;
};

struct task_executor_t {
	task_scheduler_t* scheduler;
	thread_t          thread;
	semaphore_t       signal;
	task_t            task;
	task_arg_t        arg;
	tick_t            when;
	atomic32_t        flag;
};

struct task_instance_t {
	task_t            task;
	task_arg_t        arg;
	tick_t            when;
	atomic32_t        next;
};

struct task_scheduler_t {
	thread_t          thread;
	semaphore_t       signal;
	bool              running;
	bool              idle;
	task_executor_t*  executor;
	atomic32_t        queue;
	atomic32_t        free;
	size_t            num_slots;
	task_instance_t   slots[];
};

struct task_config_t {
	int __unused;
};

static FOUNDATION_FORCEINLINE task_return_t
task_return(task_result_t result, int value) {
	return (task_return_t){result, value};
}
