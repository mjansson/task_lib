/* build.h  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

/*! \file build.h
    Build setup */

#include <foundation/platform.h>

#include <task/types.h>

/*! \define BUILD_TASK_ENABLE_DEBUG_LOG
Enable debug log output during task scheduling and execution */
#define BUILD_TASK_ENABLE_DEBUG_LOG    0

/*! \define BUILD_TASK_ENABLE_STATISTICS
Enable statistics collection during task scheduling and execution */
#define BUILD_TASK_ENABLE_STATISTICS   1
