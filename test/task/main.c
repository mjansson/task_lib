/* main.c  -  Task test for task library  -  MIT License  -  2014 Mattias Jansson / Rampant Pixels
 * 
 * This library provides a fork of the LuaJIT library with custom modifications for projects
 * based on our foundation library.
 * 
 * The latest source code maintained by Rampant Pixels is always available at
 * https://github.com/rampantpixels/lua_lib
 * 
 * For more information about LuaJIT, see
 * http://luajit.org/
 *
 * The MIT License (MIT)
 * Copyright (c) 2013 Rampant Pixels AB
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <task/task.h>
#include <foundation/foundation.h>
#include <test/test.h>


application_t test_task_application( void )
{
	application_t app = {0};
	app.name = "Task tests";
	app.short_name = "test_task_task";
	app.config_dir = "test_task_task";
	app.flags = APPLICATION_UTILITY;
	return app;
}


memory_system_t test_task_memory_system( void )
{
	return memory_system_malloc();
}


int test_task_initialize( void )
{
	return task_initialize( 0 );
}


void test_task_shutdown( void )
{
	task_shutdown();
}


DECLARE_TEST( task, single )
{	
	return 0;
}


DECLARE_TEST( task, multiple )
{
	return 0;
}


DECLARE_TEST( task, load )
{
	return 0;
}


void test_task_declare( void )
{
	ADD_TEST( task, single );
	ADD_TEST( task, multiple );
	ADD_TEST( task, load );
}


test_suite_t test_task_suite = {
	test_task_application,
	test_task_memory_system,
	test_task_declare,
	test_task_initialize,
	test_task_shutdown
};


#if FOUNDATION_PLATFORM_ANDROID

int test_task_run( void )
{
	test_suite = test_task_suite;
	return test_run_all();
}

#else

test_suite_t test_suite_define( void )
{
	return test_task_suite;
}

#endif
