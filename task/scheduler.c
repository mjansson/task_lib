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
	volatile tick_t                when;
};

struct _task_instance
{
	object_t                       task;
	task_arg_t                     arg;
	tick_t                         when;
	volatile int32_t               next;
};

struct _task_scheduler
{
	object_t                       master_thread;
	semaphore_t                    master_signal;
	task_executor_t*               executor;
	volatile int32_t               queue;
	volatile int32_t               free;
	task_instance_t                slots[BUILD_SIZE_SCHEDULER_QUEUE];
};


static void* task_executor( object_t thread, void* arg );
static void* task_scheduler( object_t thread, void* arg );


task_scheduler_t* task_scheduler_allocate( void )
{
	task_scheduler_t* scheduler = memory_allocate_zero_context( HASH_TASK, sizeof( task_scheduler_t ), 0, MEMORY_PERSISTENT );
	semaphore_initialize( &scheduler->master_signal, 0 );

	for( int islot = 0; islot < BUILD_SIZE_SCHEDULER_QUEUE; ++islot )
		scheduler->slots[islot].next = islot + 1;
	scheduler->slots[BUILD_SIZE_SCHEDULER_QUEUE-1].next = -1;

	scheduler->queue = -1;
	scheduler->free = 0;

	return scheduler;
}


void task_scheduler_deallocate( task_scheduler_t* scheduler )
{
	if( !scheduler )
		return;

	task_scheduler_stop( scheduler );

	while( scheduler->queue != -1 )
	{
		task_free( scheduler->slots[scheduler->queue].task );
		scheduler->queue = scheduler->slots[scheduler->queue].next;
	}

	semaphore_destroy( &scheduler->master_signal );

	for( unsigned int iexec = 0, execsize = array_size( scheduler->executor ); iexec < execsize; ++iexec )
		semaphore_destroy( &scheduler->executor[iexec].signal );
	array_deallocate( scheduler->executor );

	memory_deallocate( scheduler );
}


static bool _task_scheduler_queue( task_scheduler_t* scheduler, const object_t task, task_arg_t arg, tick_t when )
{
	int32_t slot, next, prev;
	do
	{
		//Grab a free slot
		slot = scheduler->free;
		if( slot >= 0 )
		{
			next = scheduler->slots[slot].next;

		 	if( atomic_cas32( &scheduler->free, next, slot ) )
			{
				task_instance_t* instance = scheduler->slots + slot;
				instance->task = task;
				instance->arg = arg;
				instance->when = when;

				task_ref( task );

				//Add it to queue
				do
				{
					prev = scheduler->queue;
					instance->next = prev;
				} while( !atomic_cas32( &scheduler->queue, slot, prev ) );

				if( scheduler->master_thread )
					semaphore_post( &scheduler->master_signal );

				return true;
			}
		}
	} while( slot >= 0 );

	log_errorf( HASH_TASK, ERROR_OUT_OF_MEMORY, "Unable to queue task to task scheduler %" PRIfixPTR ", queue full", scheduler );
	return false;
}


void task_scheduler_queue( task_scheduler_t* scheduler, const object_t task, task_arg_t arg, tick_t when )
{
	if( !when )
		when = time_current();

	if( _task_scheduler_queue( scheduler, task, arg, when ) )
	{
		if( scheduler->master_thread )
			semaphore_post( &scheduler->master_signal );
	}
	else
	{
		log_errorf( HASH_TASK, ERROR_OUT_OF_MEMORY, "Unable to queue task to task scheduler %" PRIfixPTR ", queue full", scheduler );
	}
}


void task_scheduler_multiqueue( task_scheduler_t* scheduler, unsigned int num, const object_t* tasks, const task_arg_t* args, tick_t* when )
{
	bool added = false;
	tick_t current_time = 0;

	for( unsigned int it = 0; it < num; ++it )
	{
		object_t current_task = *tasks++;
		task_arg_t* current_arg = args ? *args++ : 0;
		tick_t current_when = when ? *when++ : 0;

		if( !current_when )
		{
			if( !current_time )
				current_time = time_current();
			current_when = current_time;
		}

		if( _task_scheduler_queue( scheduler, current_task, current_arg, current_when ) )
		{
			added = true;
		}
		else
		{
			log_errorf( HASH_TASK, ERROR_OUT_OF_MEMORY, "Unable to queue task to task scheduler %" PRIfixPTR ", queue full", scheduler );
			break;
		}
	}

	if( added && scheduler->master_thread )
		semaphore_post( &scheduler->master_signal );
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


static void _task_execute( task_scheduler_t* scheduler, task_t* task, void* arg, tick_t when )
{
	tick_t starttime = time_current();
#if !BUILD_DEPLOY
	log_debugf( HASH_TASK, "Task latency: %.5fms (%lld ticks) for %s", 1000.0f * (float)time_ticks_to_seconds( starttime - when ), ( starttime - when ), task->name ? task->name : "<unknown>" );
#endif

	error_context_push( "executing task", task->name ? task->name : "<unknown>" );

	task_return_t ret = task->function( task->object, arg );
	if( ret.result == TASK_YIELD )
	{
		task_scheduler_queue( scheduler, task->id, arg, ( ret.value > 0 ) ? ( starttime + ret.value ) : 0 );
	} //else finished or aborted, so done

	error_context_pop();

#if !BUILD_DEPLOY
	tick_t endtime = time_current();
	log_debugf( HASH_TASK, "Task execution: %.5fms (%lld ticks) for %s", 1000.0f * (float)time_ticks_to_seconds( endtime - starttime ), ( endtime - starttime ), task->name ? task->name : "<unknown>" );
#endif
}


void task_scheduler_start( task_scheduler_t* scheduler )
{
	if( scheduler->master_thread )
		return;

	log_infof( HASH_TASK, "Starting task scheduler 0x%" PRIfixPTR " with %d executor threads", scheduler, task_scheduler_executor_count( scheduler ) );

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

	log_infof( HASH_TASK, "Terminating task scheduler 0x%" PRIfixPTR " with %d executor threads", scheduler, task_scheduler_executor_count( scheduler ) );

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
	tick_t enter_time, current_time;
	int32_t pending, slot, last, next, free, last_free;

	if( scheduler->queue < 0 )
		return;

	profile_begin_block( "task step" );

	enter_time = current_time = time_current();

	do
	{
		pending = scheduler->queue;
	} while( !atomic_cas32( &scheduler->queue, -1, pending ) );

	last = free = last_free = -1;
	slot = pending;

	if( pending >= 0 ) do
	{
		bool waiting = false;
		task_instance_t instance = scheduler->slots[slot];
		object_t id = instance.task;
		next = instance.next;
		
		if( !instance.when || ( instance.when <= current_time ) )
		{
			task_t* task = objectmap_lookup( _task_map, id );
			if( task )
			{
				_task_execute( scheduler, task, instance.arg, instance.when );
			}
			else
			{
				//invalid, remove from queue
				log_warnf( HASH_TASK, WARNING_BAD_DATA, "Task scheduler 0x%" PRIfixPTR " abandoning dangling task %llx in slot %d", scheduler, id, slot );
			}
		}
		else
		{
			//waiting, keep in queue
			waiting = true;
		}

		if( !waiting )
		{
			task_free( id );
			if( pending == slot )
				pending = next;
			else
				scheduler->slots[last].next = next;
			scheduler->slots[slot].next = free;
			free = slot;
			if( last_free == -1 )
				last_free = slot;
		}
		else
		{
			last = slot;
		}

		slot = next;
	}
	while( ( time_elapsed( enter_time ) * 1000.0 < limit_ms ) && ( slot >= 0 ) ); //limit_ms=0 executes one single task

	//Reinsert completed tasks as free
	if( free >= 0 ) do
	{
		slot = scheduler->free;
		scheduler->slots[last_free].next = slot;
	} while( !atomic_cas32( &scheduler->free, free, slot ) );

	//Reinsert non-executed/yielded tasks
	if( last >= 0 ) do
	{
		slot = scheduler->queue;
		scheduler->slots[last].next = slot;
	} while( !atomic_cas32( &scheduler->queue, pending, slot ) );

	profile_end_block();
}


static void* task_executor( object_t thread, void* arg )
{
	task_executor_t* executor = arg;

	while( !thread_should_terminate( thread ) )
	{
		semaphore_wait( &executor->signal );

		object_t id = executor->task;

		task_t* task = objectmap_lookup( _task_map, id );
		if( task && task->function )
			_task_execute( executor->scheduler, task, executor->arg, executor->when );

		executor->task = 0;
		task_free( id );

		semaphore_post( &executor->scheduler->master_signal );
	}

	return 0;
}


static void* task_scheduler( object_t thread, void* arg )
{
	task_scheduler_t* scheduler = arg;

	while( !thread_should_terminate( thread ) )
	{
		//Start queued tasks
		tick_t current_time;
		tick_t next_task_time = 0;
		int32_t pending, slot, last, next, free, last_free;

		profile_begin_block( "task schedule" );

		current_time = time_current();

		do
		{
			pending = scheduler->queue;
		} while( !atomic_cas32( &scheduler->queue, -1, pending ) );

		last = free = last_free = -1;
		slot = pending;

		if( pending >= 0 ) do
		{
			bool done = false;
			task_instance_t instance = scheduler->slots[slot];
			object_t id = instance.task;
			next = instance.next;
			
			if( !instance.when || ( instance.when <= current_time ) )
			{
				task_t* task = objectmap_lookup( _task_map, id );
				if( task )
				{
					for( unsigned int executor_slot = 0, executor_count = task_scheduler_executor_count( scheduler ); !done && ( executor_slot < executor_count ); ++executor_slot )
					{
						task_executor_t* executor = scheduler->executor + executor_slot;
						if( !executor->task )
						{
							executor->arg = instance.arg;
							executor->task = id;
							executor->when = instance.when;
							semaphore_post( &executor->signal );
							done = true;
						}
					}
					
					if( !done )
						thread_yield(); //unable to find free executor, yield timeslice
				}
				else
				{
					//invalid, remove from queue
					log_warnf( HASH_TASK, WARNING_BAD_DATA, "Task scheduler 0x%" PRIfixPTR " abandoning dangling task %llx in slot %d", scheduler, id, slot );
					task_free( id );
					done = true;
				}
			}
			else
			{
				//waiting, keep in queue
				if( !next_task_time || ( instance.when < next_task_time ) )
					next_task_time = instance.when;
			}

			if( done )
			{
				if( pending == slot )
					pending = next;
				else
					scheduler->slots[last].next = next;
				scheduler->slots[slot].next = free;
				free = slot;
				if( last_free == -1 )
					last_free = slot;
			}
			else
			{
				last = slot;
			}

			slot = next;
		}
		while( slot >= 0 ); //limit_ms=0 executes one single task

		//Reinsert completed tasks as free
		if( free >= 0 ) do
		{
			slot = scheduler->free;
			scheduler->slots[last_free].next = slot;
		} while( !atomic_cas32( &scheduler->free, free, slot ) );

		//Reinsert non-executed/yielded tasks
		if( last >= 0 ) do
		{
			slot = scheduler->queue;
			scheduler->slots[last].next = slot;
		} while( !atomic_cas32( &scheduler->queue, pending, slot ) );

		profile_end_block();

		if( !next_task_time )
			semaphore_wait( &scheduler->master_signal );
		else
			semaphore_try_wait( &scheduler->master_signal, (int)( REAL_C(1000.0) * time_ticks_to_seconds( next_task_time - time_current() ) ) );
	}

	return 0;
}

