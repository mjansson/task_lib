/* task.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file task.h
    Task library entry points */

#include <foundation/platform.h>

#include <task/types.h>
#include <task/hashstrings.h>
#include <task/scheduler.h>


//! Initialize task library
/*! \param num_tasks                                    Maximum number of allocated tasks, 0 for default
    \return                                             0 if success, <0 if error */
TASK_API int                                            task_initialize( unsigned int num_tasks );

//! Shutdown task library
TASK_API void                                           task_shutdown( void );


//! Create a task
/*! \param fn                                           Task function
    \param obj                                          Associated object (if any, pass 0 if not needed)
    \return                                             New task object */
TASK_API object_t                                       task_create( task_fn fn, object_t obj );

//! Add reference to task
/*! \param task                                         Task object */
TASK_API void                                           task_ref( const object_t task );

//! Destroy a task
/*! \param task                                         Task object to destroy */
TASK_API void                                           task_free( const object_t task );

//! Query if task is valid
/*! \param task                                         Task object
    \return                                             true if valid task object, false if not */
TASK_API bool                                           task_is_valid( const object_t task );
