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
	log_set_suppress( HASH_TASK, /*ERRORLEVEL_NONE*/ERRORLEVEL_INFO );
	return task_initialize( 0 );
}


void test_task_shutdown( void )
{
	task_shutdown();
}


volatile int32_t _task_counter = 0;


static task_return_t task_test( const object_t obj, task_arg_t arg )
{
	log_infof( HASH_TASK, "Task executing" );
	atomic_incr32( &_task_counter );
	return task_return( TASK_FINISH, 0 );
}


static task_return_t task_yield( const object_t obj, task_arg_t arg )
{
	int* valuearg = arg;
	if( *valuearg )
	{
		log_info( HASH_TASK, "Yield task finishing" );
		atomic_incr32( &_task_counter );
		return task_return( TASK_FINISH, 0 );
	}
	log_info( HASH_TASK, "Yield task yielding" );
	(*valuearg)++;
	return task_return( TASK_YIELD, random32_range( 10, 100 ) * ( time_ticks_per_second() / 1000 ) );
}


static task_return_t task_load( const object_t obj, task_arg_t arg )
{
	int i;
	for( i = 0; i < 1024; ++i )
	{
		if( random_range( 0, 1 ) > 1 )
			break;
	}
	atomic_incr32( &_task_counter );
	return task_return( TASK_FINISH, 0 );
}


DECLARE_TEST( task, single )
{
	task_scheduler_t* scheduler = task_scheduler_allocate();
	object_t task = task_create( task_test, 0 );

	_task_counter = 0;

	task_scheduler_set_executor_count( scheduler, 4 );
	task_scheduler_start( scheduler );
	
	thread_sleep( 100 );
	task_scheduler_queue( scheduler, task, 0, 0 );
	thread_sleep( 100 );

	EXPECT_EQ( _task_counter, 1 );

	task_scheduler_stop( scheduler );

	task_scheduler_queue( scheduler, task, 0, 0 );
	task_scheduler_start( scheduler );

	thread_sleep( 1000 );

	EXPECT_EQ( _task_counter, 2 );

	task_scheduler_stop( scheduler );

	task_scheduler_queue( scheduler, task, 0, 0 );
	task_scheduler_step( scheduler, 0 );

	EXPECT_EQ( _task_counter, 3 );

	task_scheduler_queue( scheduler, task, 0, time_current() + time_ticks_per_second() );
	task_scheduler_step( scheduler, 0 );

	EXPECT_EQ( _task_counter, 3 );

	thread_sleep( 10 );
	task_scheduler_step( scheduler, 0 );

	EXPECT_EQ( _task_counter, 3 );
	
	thread_sleep( 1000 );
	task_scheduler_step( scheduler, 0 );

	EXPECT_EQ( _task_counter, 4 );

	task_scheduler_start( scheduler );
	task_scheduler_queue( scheduler, task, 0, time_current() + time_ticks_per_second() );

	thread_sleep( 1010 );

	EXPECT_EQ( _task_counter, 5 );
	
	task_scheduler_queue( scheduler, task, 0, time_current() + time_ticks_per_second() ); //test that queued tasks are freed on scheduler destroy
	task_scheduler_deallocate( scheduler );
	task_free( task );

	EXPECT_FALSE( task_is_valid( task ) );

	return 0;
}


DECLARE_TEST( task, multiple )
{
	task_scheduler_t* scheduler = task_scheduler_allocate();
	object_t task[4] = {
		task_create( task_test, 0 ),
		task_create( task_test, 0 ),
		task_create( task_test, 0 ),
		task_create( task_test, 0 )
	};
	
	_task_counter = 0;

	task_scheduler_set_executor_count( scheduler, 4 );
	task_scheduler_start( scheduler );
	
	thread_sleep( 100 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	task_scheduler_queue( scheduler, task[0], 0, 0 );
	task_scheduler_queue( scheduler, task[1], 0, 0 );
	task_scheduler_queue( scheduler, task[2], 0, 0 );
	task_scheduler_queue( scheduler, task[3], 0, 0 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	thread_sleep( 100 );

	EXPECT_EQ( _task_counter, 16 );

	task_scheduler_stop( scheduler );

	_task_counter = 0;

	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	task_scheduler_queue( scheduler, task[0], 0, 0 );
	task_scheduler_queue( scheduler, task[1], 0, 0 );
	task_scheduler_queue( scheduler, task[2], 0, 0 );
	task_scheduler_queue( scheduler, task[3], 0, 0 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );

	task_scheduler_start( scheduler );
	thread_sleep( 100 );

	EXPECT_EQ( _task_counter, 16 );

	task_scheduler_stop( scheduler );
	_task_counter = 0;

	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	task_scheduler_queue( scheduler, task[0], 0, 0 );
	task_scheduler_queue( scheduler, task[1], 0, 0 );
	task_scheduler_queue( scheduler, task[2], 0, 0 );
	task_scheduler_queue( scheduler, task[3], 0, 0 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
	task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );

	task_scheduler_step( scheduler, 500 );

	EXPECT_EQ( _task_counter, 16 );
	
	task_scheduler_deallocate( scheduler );
	task_free( task[0] );
	task_free( task[1] );
	task_free( task[2] );
	task_free( task[3] );

	return 0;
}


DECLARE_TEST( task, yield )
{
	task_scheduler_t* scheduler = task_scheduler_allocate();
	object_t task = task_create( task_yield, 0 );
	int arg = 0;

	object_t multitask[8] = { task, task, task, task, task, task, task, task };
	int multiarg[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	void* multiargptr[8] = { &multiarg[0], &multiarg[1], &multiarg[2], &multiarg[3], &multiarg[4], &multiarg[5], &multiarg[6], &multiarg[7] };

	_task_counter = 0;

	task_scheduler_set_executor_count( scheduler, 4 );
	task_scheduler_start( scheduler );
	
	thread_sleep( 100 );
	task_scheduler_queue( scheduler, task, &arg, 0 );
	task_free( task );
	thread_sleep( 10 );

	task_scheduler_multiqueue( scheduler, 8, multitask, multiargptr, 0 );

	EXPECT_EQ( _task_counter, 0 );

	thread_sleep( 150 );

	EXPECT_EQ( _task_counter, 9 );

	task_scheduler_deallocate( scheduler );

	return 0;
}


void* producer_thread( object_t thread, void* arg )
{
	int i;
	task_scheduler_t* scheduler = arg;
	object_t task[4] = {
		task_create( task_load, 0 ),
		task_create( task_load, 0 ),
		task_create( task_load, 0 ),
		task_create( task_load, 0 )
	};

	for( i = 0; i < 100; ++i )
	{
		task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
		task_scheduler_queue( scheduler, task[0], 0, 0 );
		task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
		task_scheduler_queue( scheduler, task[1], 0, 0 );
		task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
		task_scheduler_queue( scheduler, task[2], 0, 0 );
		task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
		task_scheduler_queue( scheduler, task[3], 0, 0 );
		task_scheduler_multiqueue( scheduler, 4, task, 0, 0 );
		thread_sleep( 100 );
	}

	task_free( task[0] );
	task_free( task[1] );
	task_free( task[2] );
	task_free( task[3] );
	
	return 0;
}


DECLARE_TEST( task, load )
{
	unsigned int i;
	object_t thread[32];
	task_scheduler_t* scheduler = task_scheduler_allocate();
	unsigned int num_threads = system_hardware_threads() + 1;

	if( num_threads > 32 )
		num_threads = 32;
	
	_task_counter = 0;
	
	task_scheduler_set_executor_count( scheduler, num_threads );
	task_scheduler_start( scheduler );

	for( i = 0; i < num_threads; ++i )
	{
		thread[i] = thread_create( producer_thread, "task_producer", THREAD_PRIORITY_NORMAL, 0 );
		thread_start( thread[i], scheduler );
	}

	test_wait_for_threads_startup( thread, num_threads );

	for( i = 0; i < num_threads; ++i )
	{
		thread_terminate( thread[i] );
		thread_destroy( thread[i] );
	}

	test_wait_for_threads_exit( thread, num_threads );

	task_scheduler_deallocate( scheduler );

	EXPECT_EQ( _task_counter, 100 * 24 * (int)num_threads );
	
	return 0;
}


void test_task_declare( void )
{
	ADD_TEST( task, single );
	ADD_TEST( task, multiple );
	ADD_TEST( task, yield );
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
