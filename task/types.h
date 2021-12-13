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

/*! Task library configuration */
typedef struct task_config_t task_config_t;
/*! Task declaration */
typedef struct task_t task_t;
/*! Task scheduler control */
typedef struct task_scheduler_t task_scheduler_t;
/*! Task executor thread control */
typedef struct task_executor_t task_executor_t;
/*! Task fiber control */
typedef struct task_fiber_t task_fiber_t;
/*! Block of tasks in a queue */
typedef struct task_queue_block_t task_queue_block_t;

/*! Context type for a task */
typedef void* task_context_t;
/*! Counter for task dependencies and synchronization */
typedef atomic32_t task_counter_t;

/*! Task function */
typedef void (*task_fn)(task_context_t context);

/*! Number of tasks in one queue block */
#define TASK_QUEUE_BLOCK_CAPACITY 256

typedef enum task_fiber_state {
	TASK_FIBER_THREAD,
	TASK_FIBER_EXECUTOR,
	TASK_FIBER_FREE,
	TASK_FIBER_RUNNING,
	TASK_FIBER_YIELD,
	TASK_FIBER_FINISHED
} task_fiber_state;

/*! Task declaration */
struct task_t {
	/*! Function to execute */
	task_fn function;
	/*! Task context */
	task_context_t context;
	/*! Task counter */
	atomic32_t* counter;
};

/*! Block of tasks in a queue */
struct task_queue_block_t {
	/*! Tasks */
	task_t task[TASK_QUEUE_BLOCK_CAPACITY];
	/*! Read offset */
	size_t read;
	/*! Write offset */
	size_t write;
	/*! Next task block */
	task_queue_block_t* block_next;
};

/*! Task executor control */
struct task_executor_t {
	/*! Owning task scheduler */
	task_scheduler_t* scheduler;
	/*! Index of executor in scheduler */
	size_t index;
	/*! Execution thread */
	thread_t thread;
	/*! Currently executing fiber */
	task_fiber_t* fiber_current;
	/*! Free fiber (local to executor, cannot be accessed outside executor context) */
	task_fiber_t* fiber_free;
	/*! Fiber that was just put in waiting hold (local to executor, cannot be accessed outside executor context) */
	task_fiber_t* fiber_waiting_release;
	/*! First finished fiber index (linked list) */
	task_fiber_t* fiber_finished;
	/*! List mutex */
	mutex_t* fiber_finished_lock;
};

/*! Task fiber control */
struct task_fiber_t {
	/*! Context */
	void* context;
	/*! Thread information block */
	void* tib;
	/*! Stack pointer */
	void* stack;
	/*! Stack size */
	size_t stack_size;
	/*! Index in scheduler fiber array */
	uint index;
	/*! State */
	task_fiber_state state;
	/*! Task */
	task_t task;
	/*! Counter we are waiting on */
	atomic32_t* waiting_counter;
	/*! Fiber to return to after execution finishes */
	task_fiber_t* fiber_return;
	/*! Old fiber that should be released */
	task_fiber_t* fiber_pending_finished;
	/*! Next fiber in a linked list */
	task_fiber_t* fiber_next;
#if BUILD_ENABLE_ERROR_CONTEXT
	/*! Error context */
	void* error_context;
#endif
	/*! Platform data */
	char platform_data[FOUNDATION_FLEXIBLE_ARRAY];
};

/*! Task scheduler control */
struct task_scheduler_t {
	/*! Total size of memory block */
	size_t control_block_size;
	/*! Executors */
	task_executor_t* executor;
	/*! Number of executors */
	size_t executor_count;
	/*! Fibers */
	task_fiber_t** fiber;
	/*! Number of fibers */
	size_t fiber_count;
	/*! Total size of a fiber control block */
	size_t fiber_size;
	/*! Size of fiber context */
	size_t fiber_context_size;
	/*! Size of fiber tib */
	size_t fiber_tib_size;
	/*! Wakeup signal */
	semaphore_t signal;
	/*! Running flag */
	atomic32_t running;
	/*! Free fibers */
	task_fiber_t* fiber_free;
	/*! Current task block */
	task_queue_block_t* task_queue_block;
	/*! Last queued task block */
	task_queue_block_t* task_queue_block_tail;
	/*! Free task blocks */
	task_queue_block_t* task_free_block;
	/*! Waiting tasks/fibers */
	hashmap_t* fiber_waiting;
	/*! Lock for task blocks */
	mutex_t* task_lock;
	/*! Lock for free fibers */
	mutex_t* fiber_lock;
	/*! Lock for waiting fibers */
	mutex_t* waiting_lock;
	/* Additional fiber blocks */
	task_fiber_t** additional_fiber;
};

/*! Task library config */
struct task_config_t {
	/*! Fiber stack size */
	size_t fiber_stack_size;
};
