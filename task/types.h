/* types.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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
 * This library is put in the public domain; you can redistribute it and/or modify it without any
 * restrictions.
 *
 */

#pragma once

/*! \file types.h
    Task data types */

#include <foundation/platform.h>
#include <foundation/types.h>

#include <task/build.h>

#if defined(TASK_COMPILE) && TASK_COMPILE
#ifdef __cplusplus
#define TASK_EXTERN extern "C"
#define TASK_API extern "C"
#else
#define TASK_EXTERN extern
#define TASK_API extern
#endif
#else
#ifdef __cplusplus
#define TASK_EXTERN extern "C"
#define TASK_API extern "C"
#else
#define TASK_EXTERN extern
#define TASK_API extern
#endif
#endif

/*! Return status for a task */
typedef enum task_result_t {
	/*! Task is complete */
	TASK_FINISH = 0,
	/*! Task yielded */
	TASK_YIELD,
	/*! Task aborted */
	TASK_ABORT,
} task_result_t;

/*! Task library configuration */
typedef struct task_config_t task_config_t;
/*! Task return value */
typedef struct task_return_t task_return_t;
/*! Task declaration */
typedef struct task_t task_t;
/*! Task scheduler control */
typedef struct task_scheduler_t task_scheduler_t;
/*! Task executor thread control */
typedef struct task_executor_t task_executor_t;
/*! Task instance in scheduler queue */
typedef struct task_instance_t task_instance_t;
/*! Task scheduler statistics */
typedef struct task_statistics_t task_statistics_t;

/*! Argument type for a task */
typedef void* task_arg_t;

/*! Task function */
typedef task_return_t (*task_fn)(task_arg_t arg);

/*! Task return value. A task that yields (result is #TASK_YIELD)
can give a delay before next wakeup in the value field, as number of ticks
(see time_current and time_ticks_per_second in foundation library). */
struct task_return_t {
	/*! Task result */
	task_result_t result;
	/*! Result specific value. For TASK_YIELD result, the number of ticks
	    to delay next execution of this task */
	int value;
};

/*! Task declaration */
struct task_t {
	/*! Function to execute */
	task_fn function;
	/*! Optional name of task for profiling/debugging */
	string_const_t name;
};

/*! Task executor control */
struct task_executor_t {
	/*! Owning task scheduler */
	task_scheduler_t* scheduler;
	/*! Execution thread */
	thread_t thread;
	/*! Wakeup signal */
	semaphore_t signal;
	/*! Pending/executing task */
	task_t task;
	/*! Pending/executing task argument */
	task_arg_t arg;
	/*! When task was supposed to execute */
	tick_t when;
	/*! Flow control flag */
	atomic32_t flag;
};

/*! Task instance in scheduler queue */
struct task_instance_t {
	/*! Task */
	task_t task;
	/*! Argument */
	task_arg_t arg;
	/*! When the task should be executed */
	tick_t when;
	/*! Next task link */
	atomic32_t next;
};

/*! Task scheduler control */
struct task_scheduler_t {
	/*! Scheduler thread */
	thread_t thread;
	/*! Wakeup signal */
	semaphore_t signal;
	/*! Running flag */
	bool running;
	/*! Idle flag */
	bool idle;
#if BUILD_TASK_ENABLE_STATISTICS
	/*! Number of executed tasks */
	atomic64_t executed_count;
	/*! Latency running counter */
	atomic64_t total_latency;
	/*! Execution running counter */
	atomic64_t total_execution;
	/*! Worst latency of all tasks */
	atomic64_t maximum_latency;
	/*! Best latency of all tasks */
	atomic64_t minimum_latency;
	/*! Longest execution time of all tasks */
	atomic64_t maximum_execution;
	/*! Shortest execution time of all tasks */
	atomic64_t minimum_execution;
#endif
	/*! Executors array */
	task_executor_t* executor;
	/*! Queue of linked pending task instances */
	atomic32_t queue;
	/*! Queue of linked unused task instances */
	atomic32_t free;
	/*! Number of task instances */
	size_t slots_count;
	/*! Task instances */
	task_instance_t slots[];
};

/*! Task library config */
struct task_config_t {
	int unused;
};

/*! Task scheduler statistics. Only collected if BUILD_TASK_ENABLE_STATISTICS
is enabled */
struct task_statistics_t {
	/*! Number of executed tasks */
	size_t executed_count;
	/*! Average latency for all tasks */
	real average_latency;
	/*! Worst latency of all tasks */
	real maximum_latency;
	/*! Best latency of all tasks */
	real minimum_latency;
	/*! Average execution time for all tasks */
	real average_execution;
	/*! Longest execution time of all tasks */
	real maximum_execution;
	/*! Shortest execution time of all tasks */
	real minimum_execution;
};

/*! Convenience funktion to make a compound task return value
\param result Task result
\param value Result value */
static FOUNDATION_FORCEINLINE task_return_t
task_return(task_result_t result, int value) {
	return (task_return_t){result, value};
}
