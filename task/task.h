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


//! Initialize task library
/*! \return                                             0 if success, <0 if error */
TASK_API int                                            task_initialize( void );

//! Shutdown task library
TASK_API void                                           task_shutdown( void );
