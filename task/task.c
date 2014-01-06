/* task.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson / Rampant Pixels
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


objectmap_t* _task_map = 0;


int task_initialize( unsigned int num_tasks )
{
	if( _task_map )
		return 0;

	if( num_tasks < 64 )
		num_tasks = BUILD_SIZE_DEFAULT_NUM_TASKS;

	log_debugf( HASH_TASK, "Initializing task services (%u max tasks)", num_tasks );

	_task_map = objectmap_allocate( num_tasks );

	return 0;
}


void task_shutdown( void )
{
	if( _task_map )
		objectmap_deallocate( _task_map );
	_task_map = 0;
}


object_t task_create( task_fn fn, object_t obj )
{
	task_t* task;
	object_t object = _task_map ? objectmap_reserve( _task_map ) : 0;
	if( !object )
	{
		log_error( HASH_TASK, ERROR_OUT_OF_MEMORY, "Unable to allocate new task, map full" );	
		return 0;
	}

	task = memory_allocate_zero_context( HASH_TASK, sizeof( task_t ), 16, MEMORY_PERSISTENT );
	
	log_debugf( HASH_TASK, "Allocated task 0x%llx (0x%" PRIfixPTR ")", object, task );

	task->id = object;
	task->ref = 1;
	task->function = fn;
	task->object = obj;

	objectmap_set( _task_map, object, task );

	return object;
}


void task_ref( const object_t id )
{
	task_t* task = objectmap_lookup( _task_map, id );
	if( task )
		atomic_incr32( &task->ref );
}


void task_free( const object_t id )
{
	task_t* task;
	int32_t ref;
	do
	{
		task = objectmap_lookup( _task_map, id );
		if( task )
		{
			ref = task->ref;
			if( atomic_cas32( &task->ref, ref - 1, ref ) )
			{
				if( ref == 1 )
				{
					objectmap_free( _task_map, id );

					log_debugf( HASH_TASK, "Deallocating task 0x%llx (0x%" PRIfixPTR ")", id, task );
					memory_deallocate( task );
				}
				break;
			}
		}
	} while( task );
}

