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


typedef enum 
{
	TASK_YIELD,
	TASK_ABORT,
	TASK_FINISH
} task_result_t;


typedef struct _task_return
{
	task_result_t       result;
	int                 value;
} task_return_t;


typedef void* task_arg_t;


typedef task_return_t (* task_fn)( const object_t object, task_arg_t arg );


typedef struct _task_scheduler task_scheduler_t;


static FORCEINLINE task_return_t task_return( task_result_t result, int value ) { task_return_t ret = { result, value }; return ret; }
