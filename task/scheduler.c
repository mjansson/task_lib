/* scheduler.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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

#include <task/task.h>
#include <task/internal.h>

#include <foundation/foundation.h>


typedef struct _task_executor task_executor_t;
typedef struct _task_instance task_instance_t;


struct _task_executor
{
	task_scheduler_t*              scheduler;
	object_t                       thread;
	semaphore_t                    signal;
	volatile object_t              task;
	volatile task_arg_t            arg;
};


struct _task_instance
{
	object_t                       task;
	task_arg_t                     arg;
	tick_t                         when;
};

struct _task_scheduler
{
	object_t                       master_thread;
	semaphore_t                    master_signal;
	task_executor_t*               executor;
	semaphore_t                    queue_signal;
	volatile unsigned int          queue_count;
	task_instance_t                queue[BUILD_SIZE_SCHEDULER_QUEUE];
};


static void* task_executor( object_t thread, void* arg );
static void* task_scheduler( object_t thread, void* arg );


task_scheduler_t* task_scheduler_allocate( void )
{
	task_scheduler_t* scheduler = memory_allocate_zero_context( HASH_TASK, sizeof( task_scheduler_t ), 0, MEMORY_PERSISTENT );
	semaphore_initialize( &scheduler->master_signal, 0 );
	semaphore_initialize( &scheduler->queue_signal, 1 );
	return scheduler;	
}


void task_scheduler_deallocate( task_scheduler_t* scheduler )
{
	if( !scheduler )
		return;

	task_scheduler_stop( scheduler );

	semaphore_destroy( &scheduler->master_signal );
	semaphore_destroy( &scheduler->queue_signal );

	for( unsigned int iexec = 0, execsize = array_size( scheduler->executor ); iexec < execsize; ++iexec )
		semaphore_destroy( &scheduler->executor[iexec].signal );
	array_deallocate( scheduler->executor );

	memory_deallocate( scheduler );
}


void task_scheduler_queue( task_scheduler_t* scheduler, const object_t task, task_arg_t arg, tick_t when )
{
	bool do_signal = ( scheduler->master_thread != 0 );

	if( do_signal )
		semaphore_wait( &scheduler->queue_signal );

	if( scheduler->queue_count >= BUILD_SIZE_SCHEDULER_QUEUE )
	{
		log_errorf( HASH_TASK, ERROR_OUT_OF_MEMORY, "Unable to queue task to task scheduler %" PRIfixPTR ", queue full", scheduler );
	}
	else
	{
		task_instance_t* instance = scheduler->queue + scheduler->queue_count++;
		instance->task = task;
		instance->arg = arg;
		instance->when = when;
	}

	if( do_signal )
	{
		semaphore_post( &scheduler->queue_signal );
		semaphore_post( &scheduler->master_signal );
	}
}


void task_scheduler_multiqueue( task_scheduler_t* scheduler, unsigned int num, const object_t* tasks, const task_arg_t* args, tick_t* when )
{
	bool do_signal = ( scheduler->master_thread != 0 );

	if( do_signal )
		semaphore_wait( &scheduler->queue_signal );

	if( scheduler->queue_count + num >= BUILD_SIZE_SCHEDULER_QUEUE )
	{
		log_errorf( HASH_TASK, ERROR_OUT_OF_MEMORY, "Unable to multiqueue tasks to task scheduler %" PRIfixPTR ", queue full", scheduler );
	}
	else
	{
		task_instance_t* instance = scheduler->queue + scheduler->queue_count;
		for( unsigned int it = 0; it < num; ++it, ++instance )
		{
			instance->task = *tasks++;
			instance->arg = args ? *args++ : 0;
			instance->when = when ? *when++ : 0;
		}
		scheduler->queue_count += num;
	}

	if( do_signal )
	{
		semaphore_post( &scheduler->queue_signal );
		semaphore_post( &scheduler->master_signal );
	}
}


unsigned int task_scheduler_executor_count( task_scheduler_t* scheduler )
{
	return array_size( scheduler->executor );
}


void task_scheduler_set_executor_count( task_scheduler_t* scheduler, unsigned int num )
{
	bool was_running = ( scheduler->master_thread != 0 );
	unsigned int previous = task_scheduler_executor_count( scheduler );

	if( was_running )
		task_scheduler_stop( scheduler );

	if( num == previous )
		return;

	//TODO: If 1 executor, merge scheduler and executor into a single wait-step executor
	memory_context_push( HASH_TASK );

	if( previous < num )
	{
		array_grow( scheduler->executor, num - previous );

		for( unsigned int iexec = previous; iexec < num; ++iexec )
		{
			task_executor_t* executor = scheduler->executor + iexec;
			executor->scheduler = scheduler;
			executor->thread = 0;
			executor->task = 0;
			semaphore_initialize( &executor->signal, 0 );
		}
	}
	else
	{
		for( unsigned int iexec = num; iexec < previous; ++iexec )
		{
			task_executor_t* executor = scheduler->executor + iexec;
			semaphore_destroy( &executor->signal );
		}

		array_resize( scheduler->executor, num );
	}

	memory_context_pop();

	if( was_running )
		task_scheduler_start( scheduler );
}


void task_scheduler_start( task_scheduler_t* scheduler )
{
	if( scheduler->master_thread )
		return;

	log_infof( HASH_TASK, "Starting task scheduler %" PRIfixPTR " with %d executor threads", task_scheduler_executor_count( scheduler ) );

	for( unsigned int iexec = 0, execsize = array_size( scheduler->executor ); iexec < execsize; ++iexec )
	{
		task_executor_t* executor = scheduler->executor + iexec;
		executor->thread = thread_create( task_executor, "task_executor", THREAD_PRIORITY_NORMAL, 0 );
		thread_start( executor->thread, executor );
	}

	scheduler->master_thread = thread_create( task_scheduler, "task_scheduler", THREAD_PRIORITY_NORMAL, 0 );
	thread_start( scheduler->master_thread, scheduler );
}


void task_scheduler_stop( task_scheduler_t* scheduler )
{
	if( !scheduler->master_thread )
		return;

	log_infof( HASH_TASK, "Terminating task scheduler %" PRIfixPTR " with %d executor threads", task_scheduler_executor_count( scheduler ) );

	thread_terminate( scheduler->master_thread );
	semaphore_post( &scheduler->master_signal );
	thread_destroy( scheduler->master_thread );

	for( unsigned int iexec = 0, execsize = array_size( scheduler->executor ); iexec < execsize; ++iexec )
	{
		task_executor_t* executor = scheduler->executor + iexec;
		thread_terminate( executor->thread );
		semaphore_post( &executor->signal );
		thread_destroy( executor->thread );
	}

	for( unsigned int iexec = 0, execsize = array_size( scheduler->executor ); iexec < execsize; ++iexec )
	{
		task_executor_t* executor = scheduler->executor + iexec;
		while( thread_is_running( executor->thread ) )
			thread_yield();
		executor->thread = 0;
	}

	while( thread_is_running( scheduler->master_thread ) )
		thread_yield();

	scheduler->master_thread = 0;
}


void task_scheduler_step( task_scheduler_t* scheduler, unsigned int limit_ms )
{
	if( !scheduler->queue_count || scheduler->master_thread )
		return;

	profile_begin_block( "task step" );

	tick_t enter_time = time_current();

	semaphore_wait( &scheduler->queue_signal );

	tick_t current_time = time_current();
	unsigned int it = 0;
	do
	{
		//TODO: Tasks might spawn new tasks, causing semaphore deadlock...
		task_instance_t instance = scheduler->queue[it];
		if( !instance.when || ( instance.when <= current_time ) )
		{
			object_t id = instance.task;
			task_ref( id );
			task_t* task = objectmap_lookup( _task_map, id );
			if( task )
			{
				task_result_t res = task->function( task->object, instance.arg );
				if( res == TASK_YIELD )
				{
					++it;
				}
				else //finished or aborted, swap with last to remove
				{
					scheduler->queue[it] = scheduler->queue[scheduler->queue_count--];
				}
				task_free( id );
			}
			else
			{
				log_warnf( HASH_TASK, WARNING_BAD_DATA, "Task scheduler %" PRIfixPTR " abandoning dangling task %llx in slot %d", scheduler, id, it );
				scheduler->queue[it] = scheduler->queue[scheduler->queue_count--];
			}
		}
		else
		{
			++it;
		}
	} while( ( time_elapsed( enter_time ) * 1000.0 < limit_ms ) && ( it < scheduler->queue_count ) ); //limit_ms=0 executes one single task

	semaphore_post( &scheduler->queue_signal );

	profile_end_block();
}


static void* task_executor( object_t thread, void* arg )
{
	task_executor_t* executor = arg;

	while( !thread_should_terminate( thread ) )
	{
		semaphore_wait( &executor->signal );

		object_t id = executor->task;
		task_ref( id );

		task_t* task = objectmap_lookup( _task_map, id );
		if( task && task->function )
		{
			const char* name = task->name;

#if !BUILD_DEPLOY
			tick_t starttime = time_current();
			log_debugf( HASH_TASK, "Task latency: %.5fms (%lld ticks) for %s", 1000.0f * (float)time_ticks_to_seconds( starttime - task->timestamp ), ( starttime - task->timestamp ), name ? name : "<unknown>" );
#endif

			error_context_push( "executing task", name );

			task_result_t res = task->function( task->object, executor->arg );
			if( res == TASK_YIELD )
			{
				task_scheduler_queue( executor->scheduler, id, executor->arg, 0 );
			} //else finished or aborted, so done

			error_context_pop();

#if !BUILD_DEPLOY
			tick_t endtime = time_current();
			log_debugf( HASH_TASK, "Task execution: %.5fms (%lld ticks) for %s", 1000.0f * (float)time_ticks_to_seconds( endtime - starttime ), ( endtime - starttime ), name ? name : "<unknown>" );
#endif

			semaphore_post( &executor->scheduler->master_signal );
		}

		executor->task = 0;
		task_free( id );
	}

	return 0;
}


static void* task_scheduler( object_t thread, void* arg )
{
	task_scheduler_t* scheduler = arg;

	while( !thread_should_terminate( thread ) )
	{
		//Start queued tasks
		semaphore_wait( &scheduler->queue_signal );

		tick_t current_time = time_current();
		tick_t next_task_time = 0;

		unsigned int queue_count = scheduler->queue_count;
		unsigned int ti = 0, slot = 0, executor_count = 0;
		task_instance_t* instance = scheduler->queue;
		for( ; ti < queue_count; ++ti, ++instance )
		{
			if( !instance->when || ( instance->when <= current_time ) )
			{
				object_t id = instance->task;
				task_ref( id );
				task_t* task = objectmap_lookup( _task_map, id );
				if( !task )
				{
					instance->task = 0;
					continue;
				}
				bool started = false;
				for( slot = 0, executor_count = task_scheduler_executor_count( scheduler ); !started && ( slot < executor_count ); ++slot )
				{
					task_executor_t* executor = scheduler->executor + slot;
					if( !executor->task )
					{
						executor->arg = instance->arg;
						executor->task = id;
						semaphore_post( &executor->signal );
						instance->task = 0;
						started = true;
					}
				}
				if( !started )
					break; //No more free executor slots
			}
			else if( instance->when )
			{
				if( !next_task_time || ( instance->when < next_task_time ) )
					next_task_time = instance->when;
			}
		}

		//Compress the queue
		instance = scheduler->queue;
		for( ti = 0; ti < queue_count; ++ti, ++instance )
		{
			if( !instance->task )
			{
				//Move next non-empty task down
				unsigned int next_ti = ti + 1;
				task_instance_t* next_instance = instance + 1;
				for( ; next_ti < queue_count; ++next_ti, ++next_instance )
				{
					if( next_instance->task )
					{
						*instance = *next_instance;
						next_instance->task = 0;
						break;
					}
				}

				if( next_ti >= queue_count )
					break;
			}
		}

		scheduler->queue_count = ti;

		semaphore_post( &scheduler->queue_signal );

		if( !next_task_time )
			semaphore_wait( &scheduler->master_signal );
		else
			semaphore_try_wait( &scheduler->master_signal, (int)( REAL_C(1000.0) * time_ticks_to_seconds( next_task_time - time_current() ) ) );
	}

	return 0;
}

