/* scheduler.c  -  Task library  -  Public Domain  -  2013 Mattias Jansson
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

#include "task.h"

#include <foundation/foundation.h>

#include <foundation/windows.h>
#include <foundation/posix.h>

#if FOUNDATION_PLATFORM_APPLE
#define _XOPEN_SOURCE
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#if FOUNDATION_PLATFORM_POSIX
#include <ucontext.h>
#include <sys/mman.h>
#ifndef MAP_UNINITIALIZED
#define MAP_UNINITIALIZED 0
#endif
#endif

// Round up to nearest system memory page size multiple
#define round_to_page_size(size) (page_size * ((size + (page_size - 1)) / page_size))

extern task_executor_t*
task_executor_thread_current(void);

task_scheduler_t*
task_scheduler_allocate(size_t executor_count, size_t fiber_count) {
	size_t hwthread_count = system_hardware_threads();
	if (!executor_count)
		executor_count = hwthread_count;
	if (!fiber_count)
		fiber_count = executor_count * 16;

	executor_count = math_clamp(executor_count, 1, 1024);
	fiber_count = math_clamp(fiber_count, (hwthread_count * 2), 4096);

	// Align all blocks to system memory page size to make stack blocks
	// system memory page aligned.
	size_t page_size = 4096;
#if FOUNDATION_PLATFORM_WINDOWS
	SYSTEM_INFO system_info;
	memset(&system_info, 0, sizeof(system_info));
	GetSystemInfo(&system_info);
	page_size = (size_t)system_info.dwPageSize;
#else
	page_size = (size_t)sysconf(_SC_PAGESIZE);
#endif

	size_t fiber_size = sizeof(task_fiber_t);
#if FOUNDATION_PLATFORM_WINDOWS
	size_t context_size = sizeof(CONTEXT);
	size_t tib_size = sizeof(NT_TIB);
#elif FOUNDATION_PLATFORM_POSIX
	size_t context_size = sizeof(ucontext_t);
	size_t tib_size = 0;
#if FOUNDATION_PLATFORM_APPLE
	tib_size = sizeof(*dummy_context->uc_mcontext);
#endif
#else
#error Not implemented
#endif
	// Align to 16 bytes
	fiber_size = 16 * ((fiber_size + 15) / 16);
	context_size = 16 * ((context_size + 15) / 16);
	tib_size = 16 * ((tib_size + 15) / 16);
	size_t total_fiber_size = fiber_size + context_size + tib_size;

	size_t scheduler_memory_size = sizeof(task_scheduler_t);
	scheduler_memory_size += sizeof(task_executor_t) * executor_count;
	scheduler_memory_size += sizeof(task_fiber_t*) * fiber_count;
	scheduler_memory_size = round_to_page_size(scheduler_memory_size);

	size_t fiber_offset = scheduler_memory_size;
	scheduler_memory_size += total_fiber_size * fiber_count;
	scheduler_memory_size = round_to_page_size(scheduler_memory_size);

	size_t stack_size = task_module_config().fiber_stack_size;
	stack_size = round_to_page_size(stack_size);
	size_t stack_memory_size = fiber_count * (stack_size + page_size);

	size_t control_block_size = scheduler_memory_size + stack_memory_size;

	void* memory_block = nullptr;
#if FOUNDATION_PLATFORM_WINDOWS
	memory_block = VirtualAlloc(0, control_block_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_UNINITIALIZED;
	memory_block = mmap(0, control_block_size, PROT_READ | PROT_WRITE, mmap_flags, -1, 0);
#endif

	// Setup all control block pointers and fiber stack pointers
	task_scheduler_t* scheduler = memory_block;
	scheduler->control_block_size = control_block_size;
	scheduler->fiber_size = fiber_size;
	scheduler->fiber_context_size = context_size;
	scheduler->fiber_tib_size = tib_size;

	scheduler->executor = pointer_offset(memory_block, sizeof(task_scheduler_t));
	scheduler->executor_count = executor_count;

	scheduler->fiber = pointer_offset(scheduler->executor, sizeof(task_executor_t) * executor_count);
	scheduler->fiber_count = fiber_count;

	void* scheduler_base_end = pointer_offset(scheduler->fiber, sizeof(task_fiber_t*) * fiber_count);

	scheduler->task_queue_block = memory_allocate(HASH_TASK, sizeof(task_queue_block_t), 0, MEMORY_PERSISTENT);
	scheduler->task_queue_block->block_next = nullptr;
	scheduler->task_queue_block->read = 0;
	scheduler->task_queue_block->write = 0;
	scheduler->task_queue_block_tail = scheduler->task_queue_block;
	scheduler->task_free_block = nullptr;

	scheduler->additional_fiber = nullptr;
	scheduler->fiber_waiting = hashmap_allocate(13, 8);

	scheduler->task_lock = mutex_allocate(STRING_CONST("scheduler task lock"));
	scheduler->fiber_lock = mutex_allocate(STRING_CONST("scheduler fiber lock"));
	scheduler->waiting_lock = mutex_allocate(STRING_CONST("scheduler waiting lock"));

	semaphore_initialize(&scheduler->signal, 0);

	void* fiber_start = pointer_offset(memory_block, fiber_offset);
	void* fiber_end = pointer_offset(fiber_start, total_fiber_size * fiber_count);
	FOUNDATION_ASSERT(scheduler_base_end <= fiber_start);

	void* scheduler_end_block = pointer_offset(memory_block, scheduler_memory_size);
	void* stack_pointer = scheduler_end_block;
	FOUNDATION_ASSERT(fiber_end <= stack_pointer);

	task_fiber_t* fiber_prev = nullptr;
	for (size_t ifiber = 0; ifiber < fiber_count; ++ifiber) {
		// Stack guard
#if FOUNDATION_PLATFORM_WINDOWS
		VirtualProtect(stack_pointer, page_size, PAGE_NOACCESS, 0);
#elif FOUNDATION_PLATFORM_POSIX
		mprotect(stack_pointer, page_size, PROT_NONE);
#else
#error Not implemented
#endif

		stack_pointer = pointer_offset(stack_pointer, stack_size + page_size);

		task_fiber_t* fiber = fiber_start;
		fiber->context = pointer_offset(fiber, fiber_size);
		fiber->tib = tib_size ? pointer_offset(fiber->context, context_size) : nullptr;
		// Stack starts at end of region and grows in negative address space direction
		fiber->stack = stack_pointer;
		fiber->stack_size = stack_size;
		fiber->index = (uint)ifiber;
		fiber->state = TASK_FIBER_NOT_INITIALIZED;
		fiber->fiber_next = fiber_prev;
		fiber->fiber_pending_finished = nullptr;
#if BUILD_ENABLE_ERROR_CONTEXT
		fiber->error_context = nullptr;
#endif

		scheduler->fiber[ifiber] = fiber;
		fiber_prev = fiber;
		fiber_start = pointer_offset(fiber_start, total_fiber_size);
	}

	scheduler_end_block = pointer_offset(scheduler, control_block_size);
	FOUNDATION_ASSERT(stack_pointer == scheduler_end_block);

	scheduler->fiber_free = scheduler->fiber[fiber_count - 1];

	atomic_store32(&scheduler->running, 1, memory_order_release);

	// Launch executor threads
	for (size_t iexecutor = 0; iexecutor < executor_count; ++iexecutor) {
		task_executor_t* executor = scheduler->executor + iexecutor;
		executor->scheduler = scheduler;
		executor->index = iexecutor;
		executor->fiber_finished_lock = mutex_allocate(STRING_CONST("executor finished fiber lock"));
		executor->fiber_finished = nullptr;
		thread_initialize(&executor->thread, task_executor_thread, executor, STRING_CONST("task executor"),
		                  THREAD_PRIORITY_NORMAL, (uint)stack_size);
		thread_start(&executor->thread);
	}

	return scheduler;
}

void
task_scheduler_deallocate(task_scheduler_t* scheduler) {
	atomic_store32(&scheduler->running, 0, memory_order_release);
	semaphore_post(&scheduler->signal);

	for (size_t iexecutor = 0; iexecutor < scheduler->executor_count; ++iexecutor) {
		task_executor_t* executor = scheduler->executor + iexecutor;
		thread_finalize(&executor->thread);
		mutex_deallocate(executor->fiber_finished_lock);
	}

	semaphore_finalize(&scheduler->signal);

	task_queue_block_t* block = scheduler->task_queue_block;
	while (block) {
		task_queue_block_t* next_block = block->block_next;
		memory_deallocate(block);
		block = next_block;
	}

	block = scheduler->task_free_block;
	while (block) {
		task_queue_block_t* next_block = block->block_next;
		memory_deallocate(block);
		block = next_block;
	}

	hashmap_deallocate(scheduler->fiber_waiting);

	mutex_deallocate(scheduler->waiting_lock);
	mutex_deallocate(scheduler->fiber_lock);
	mutex_deallocate(scheduler->task_lock);

#if FOUNDATION_PLATFORM_WINDOWS
	VirtualFree(scheduler, 0, MEM_RELEASE);
#else
	munmap(scheduler, scheduler->control_block_size);
#endif
}

void
task_scheduler_queue(task_scheduler_t* scheduler, task_t task) {
	if (!scheduler) {
		task.function(task.context);
		if (task.counter)
			atomic_decr32(task.counter, memory_order_relaxed);
		return;
	}

#if BUILD_DEBUG || BUILD_RELEASE
	FOUNDATION_ASSERT(task.function);
#endif

	mutex_lock(scheduler->task_lock);
	if (scheduler->task_queue_block_tail && (scheduler->task_queue_block_tail->write < TASK_QUEUE_BLOCK_CAPACITY)) {
		scheduler->task_queue_block_tail->task[scheduler->task_queue_block_tail->write++] = task;
		mutex_unlock(scheduler->task_lock);
		semaphore_post(&scheduler->signal);
		return;
	}

	task_queue_block_t* block;
	if (scheduler->task_free_block) {
		block = scheduler->task_free_block;
		scheduler->task_free_block = scheduler->task_free_block->block_next;
	} else {
		block = memory_allocate(HASH_TASK, sizeof(task_queue_block_t), 0, MEMORY_PERSISTENT);
	}

	block->block_next = nullptr;
	block->read = 0;
	block->write = 1;
	block->task[0] = task;

	if (scheduler->task_queue_block_tail) {
		scheduler->task_queue_block_tail->block_next = block;
		scheduler->task_queue_block_tail = block;
	} else {
		FOUNDATION_ASSERT(!scheduler->task_queue_block);
		scheduler->task_queue_block = block;
		scheduler->task_queue_block_tail = block;
	}

	mutex_unlock(scheduler->task_lock);
	semaphore_post(&scheduler->signal);

	/* TODO: Lock free implementation
retry:
	// Try to fit the task into the current block
	task_queue_block_t* block = scheduler->task_queue_block;
	int32_t current_write = atomic_load32(&block->write_pending, memory_order_relaxed);
	while (current_write < TASK_QUEUE_BLOCK_CAPACITY) {
	    if (atomic_cas32(&block->write_pending, current_write + 1, current_write, memory_order_relaxed,
	                     memory_order_relaxed)) {
	        block->task[current_write] = task;
	        while (!atomic_cas32(&block->write, current_write + 1, current_write, memory_order_release,
	                             memory_order_acquire)) {
	            thread_yield();
	        }
	        semaphore_post(&scheduler->signal);
	        return;
	    }
	    current_write = atomic_load32(&block->write_pending, memory_order_relaxed);
	}

	// Check if we can swap in a new free block
	block = atomic_load_ptr(&scheduler->task_free_block, memory_order_acquire);
	while (block) {
	    task_queue_block_t* next_block = atomic_load_ptr(&block->block_next, memory_order_relaxed);
	    if (atomic_cas_ptr(&scheduler->task_free_block, next_block, block, memory_order_release,
	                       memory_order_relaxed)) {
	        atomic_store32(&block->read, 0, memory_order_relaxed);
	        atomic_store32(&block->write, 0, memory_order_relaxed);
	        atomic_store32(&block->write_pending, 0, memory_order_relaxed);
	        if (atomic_cas_ptr(&scheduler->task_queue_block, block, nullptr, memory_order_release,
	                           memory_order_relaxed))
	            goto retry;
	    }
	    block = atomic_load_ptr(&scheduler->task_free_block, memory_order_acquire);
	}

	// Allocate a new block
	*/
}

void
task_scheduler_multiqueue(task_scheduler_t* scheduler, task_t* task, size_t task_count) {
	if (!scheduler) {
		for (size_t itask = 0; itask < task_count; ++itask) {
			task[itask].function(task[itask].context);
			if (task[itask].counter)
				atomic_decr32(task[itask].counter, memory_order_relaxed);
		}
		return;
	}

	mutex_lock(scheduler->task_lock);

	const task_t* current_task = task;
	size_t remain_count = task_count;
	if (scheduler->task_queue_block_tail) {
		size_t copy_count = remain_count;
		size_t max_count = TASK_QUEUE_BLOCK_CAPACITY - scheduler->task_queue_block_tail->write;
		if (copy_count > max_count)
			copy_count = max_count;
		memcpy(scheduler->task_queue_block_tail->task + scheduler->task_queue_block_tail->write, current_task,
		       sizeof(task_t) * copy_count);
		current_task += copy_count;
		scheduler->task_queue_block_tail->write += copy_count;
		remain_count -= copy_count;

		if (!remain_count) {
			mutex_unlock(scheduler->task_lock);
			semaphore_post_multiple(&scheduler->signal, (uint)task_count);
			return;
		}
	}

	while (remain_count) {
		task_queue_block_t* block;
		if (scheduler->task_free_block) {
			block = scheduler->task_free_block;
			scheduler->task_free_block = scheduler->task_free_block->block_next;
		} else {
			block = memory_allocate(HASH_TASK, sizeof(task_queue_block_t), 0, MEMORY_PERSISTENT);
		}

		size_t copy_count = remain_count;
		if (copy_count > TASK_QUEUE_BLOCK_CAPACITY)
			copy_count = TASK_QUEUE_BLOCK_CAPACITY;
		remain_count -= copy_count;

		block->block_next = nullptr;
		block->read = 0;
		block->write = copy_count;
		memcpy(block->task, current_task, sizeof(task_t) * copy_count);
		current_task += copy_count;

		if (scheduler->task_queue_block_tail) {
			scheduler->task_queue_block_tail->block_next = block;
			scheduler->task_queue_block_tail = block;
		} else {
			FOUNDATION_ASSERT(!scheduler->task_queue_block);
			scheduler->task_queue_block = block;
			scheduler->task_queue_block_tail = block;
		}
	}

	mutex_unlock(scheduler->task_lock);
	semaphore_post_multiple(&scheduler->signal, (uint)task_count);
}

bool
task_scheduler_next_task(task_scheduler_t* scheduler, task_t* task) {
	if (!scheduler->task_queue_block || !atomic_load32(&scheduler->running, memory_order_relaxed))
		return false;

	if (!mutex_lock(scheduler->task_lock))
		return false;

	task_queue_block_t* block = scheduler->task_queue_block;
	if (!block || (block->read >= block->write)) {
		mutex_unlock(scheduler->task_lock);
		return false;
	}

	size_t read = block->read++;
	*task = block->task[read];
	FOUNDATION_ASSERT(task->function);
	if (read < (TASK_QUEUE_BLOCK_CAPACITY - 1)) {
		mutex_unlock(scheduler->task_lock);
		return true;
	}

	scheduler->task_queue_block = block->block_next;
	if (!scheduler->task_queue_block)
		scheduler->task_queue_block_tail = nullptr;

	block->block_next = scheduler->task_free_block;
	scheduler->task_free_block = block;

	mutex_unlock(scheduler->task_lock);
	return true;

	/* TODO: Lock free implementation
	int32_t read = atomic_load32(&scheduler->task_queue_block->read, memory_order_relaxed);
	int32_t write = atomic_load32(&scheduler->task_queue_block->write, memory_order_relaxed);
	if (read >= write)
	    return false;

	if (atomic_cas32(&scheduler->task_queue_block->read, read + 1, read, memory_order_release, memory_order_relaxed)) {
	    *task = scheduler->task_queue_block->task[read];
	    if ((read == TASK_QUEUE_BLOCK_CAPACITY) ||
	        (read > atomic_load32(&scheduler->task_queue_block->write_pending, memory_order_relaxed))) {
	    }
	}
	*/
}

task_fiber_t*
task_scheduler_next_free_fiber(task_scheduler_t* scheduler) {
	do {
		// First check the scheduler global free list of fibers
		mutex_lock(scheduler->fiber_lock);
		task_fiber_t* fiber = scheduler->fiber_free;
		scheduler->fiber_free = fiber ? fiber->fiber_next : nullptr;
		mutex_unlock(scheduler->fiber_lock);
		if (fiber) {
			FOUNDATION_ASSERT_MSG((fiber->state == TASK_FIBER_FREE) || (fiber->state == TASK_FIBER_NOT_INITIALIZED),
			                      "Internal fiber failure, free fiber not in free state");
			FOUNDATION_ASSERT_MSG(!fiber->fiber_pending_finished,
			                      "Internal fiber failure, free fiber has pending finished fiber");
			fiber->fiber_next = nullptr;
			return fiber;
		}

		// Otherwise try to steal a fiber from one of the executor free lists, and put the
		// remainder in the scheduler global free list
		atomic_thread_fence_acquire();
		for (size_t iexecutor = 0; iexecutor < scheduler->executor_count; ++iexecutor) {
			task_executor_t* executor = scheduler->executor + iexecutor;
			if (!executor->fiber_finished)
				continue;
			// Steal the list
			mutex_lock(executor->fiber_finished_lock);
			fiber = executor->fiber_finished;
			executor->fiber_finished = nullptr;
			mutex_unlock(executor->fiber_finished_lock);

			if (fiber) {
				FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_FREE,
				                      "Internal fiber failure, free fiber not in free state");
				FOUNDATION_ASSERT_MSG(!fiber->fiber_pending_finished,
				                      "Internal fiber failure, free fiber has pending finished fiber");
				task_fiber_t* fiber_remain = fiber->fiber_next;
				if (fiber_remain) {
					task_fiber_t* fiber_last = fiber_remain;
					while (fiber_last->fiber_next)
						fiber_last = fiber_last->fiber_next;
					mutex_lock(scheduler->fiber_lock);
					fiber_last->fiber_next = scheduler->fiber_free;
					scheduler->fiber_free = fiber_remain;
					mutex_unlock(scheduler->fiber_lock);
				}
				fiber->fiber_next = nullptr;
				return fiber;
			}
		}
		thread_yield();
	} while (true);
}

#if FOUNDATION_COMPILER_MSVC
// Have to turn optimizations on in order to get variables stored in registers.
// Once the fiber is in hashmap and mutex unlocked, some other thread can pick
// up the fiber on task counter decrement and start executing in that fiber
// stack space, making it unsafe to use.
// TODO: Refactor into a fiber safe spin-lock (fibex) which is locked here
//       and unlocked in the switched to fiber
//#pragma optimize("", off)
#endif

extern void
task_fiber_set_current(task_fiber_t* fiber);

FOUNDATION_NOINLINE bool
task_scheduler_push_fiber_waiting_and_yield(task_scheduler_t* scheduler, task_fiber_t* fiber, atomic32_t* counter) {
	FOUNDATION_ASSERT_MSG(fiber->state == TASK_FIBER_RUNNING,
	                      "Internal fiber failure, fiber not in running state when pushed to waiting list");
	FOUNDATION_ASSERT(fiber->fiber_return->state == TASK_FIBER_EXECUTOR);

	if (fiber->fiber_pending_finished) {
		task_fiber_t* fiber_free = fiber->fiber_pending_finished;
		fiber->fiber_pending_finished = nullptr;
		FOUNDATION_ASSERT_MSG(fiber_free->stack, "Internal fiber failure, executor control fiber marked as free");
		FOUNDATION_ASSERT_MSG(fiber_free->state == TASK_FIBER_FINISHED,
		                      "Internal fiber failure, finished fiber not in finished state");
		fiber_free->state = TASK_FIBER_FREE;

		mutex_lock(scheduler->fiber_lock);
		fiber_free->fiber_next = scheduler->fiber_free;
		scheduler->fiber_free = fiber_free;
		mutex_unlock(scheduler->fiber_lock);
	}

	mutex_lock(scheduler->waiting_lock);
	if (atomic_load32(counter, memory_order_relaxed) > 0) {
		atomic_incr32(counter, memory_order_relaxed);

		task_executor_thread_current()->fiber_waiting_release = fiber;

		fiber->fiber_next = nullptr;
		fiber->waiting_counter = counter;
		fiber->state = TASK_FIBER_YIELD;

		hashmap_insert(scheduler->fiber_waiting, (hash_t)((uintptr_t)counter), fiber);
		mutex_unlock(scheduler->waiting_lock);

		task_fiber_switch(nullptr, fiber->fiber_return);

		exception_raise_abort();
	}
	mutex_unlock(scheduler->waiting_lock);

	return true;
}

FOUNDATION_NOINLINE task_fiber_t*
task_scheduler_pop_fiber_waiting(task_scheduler_t* scheduler, atomic32_t* counter) {
	mutex_lock(scheduler->waiting_lock);
	if (atomic_load32(counter, memory_order_relaxed) > 0) {
		mutex_unlock(scheduler->waiting_lock);
		return nullptr;
	}
	task_fiber_t* fiber = hashmap_erase(scheduler->fiber_waiting, (hash_t)((uintptr_t)counter));
	mutex_unlock(scheduler->waiting_lock);

	FOUNDATION_ASSERT_MSG(!fiber || (fiber->state == TASK_FIBER_YIELD),
	                      "Internal fiber failure, waiting fiber not in yield state when popped from waiting list");
	return fiber;
}
