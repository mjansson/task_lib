/* main.c  -  Task test for task library  -  MIT License  -  2014 Mattias Jansson
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
 * This library is put in the public domain; you can redistribute it and/or modify it without any restrictions.
 */

#include <task/task.h>
#include <foundation/foundation.h>
#include <test/test.h>

static application_t
test_task_application(void) {
	application_t app;
	memset(&app, 0, sizeof(app));
	app.name = string_const(STRING_CONST("Task tests"));
	app.short_name = string_const(STRING_CONST("test_task"));
	app.company = string_const(STRING_CONST(""));
	app.flags = APPLICATION_UTILITY;
	app.exception_handler = test_exception_handler;
	return app;
}

static memory_system_t
test_task_memory_system(void) {
	return memory_system_malloc();
}

static foundation_config_t
test_task_config(void) {
	foundation_config_t config;
	memset(&config, 0, sizeof(config));
	return config;
}

static int
test_task_initialize(void) {
	task_config_t config;
	log_set_suppress(HASH_TASK, ERRORLEVEL_NONE);
	memset(&config, 0, sizeof(config));
	config.fiber_stack_size = 16 * 1024;
	return task_module_initialize(config);
}

static void
test_task_finalize(void) {
	task_module_finalize();
}

static task_scheduler_t* task_scheduler;
static atomic32_t task_counter;
static atomic32_t remain_counter;

static struct multi_task_setup_t {
	size_t sub_task_count;
	size_t final_task_count;
} multi_task_setup;

static FOUNDATION_NOINLINE void
task_single_test(task_context_t context) {
	FOUNDATION_UNUSED(context);
	// log_infof(HASH_TASK, STRING_CONST("Task executing %d"), atomic_load32(&task_counter, memory_order_relaxed));
	atomic_incr32(&task_counter, memory_order_relaxed);
}

static FOUNDATION_NOINLINE void
task_multi_sub_test(task_context_t context) {
	struct multi_task_setup_t* setup = (struct multi_task_setup_t*)context;
	size_t sub_task_count = setup->final_task_count;
	atomic32_t sub_counter;
	atomic_store32(&sub_counter, (int32_t)sub_task_count, memory_order_relaxed);

	task_t* sub_task = memory_allocate(HASH_TEST, sizeof(task_t) * sub_task_count, 0, MEMORY_PERSISTENT);
	for (size_t itask = 0; itask < sub_task_count; ++itask) {
		sub_task[itask].function = task_single_test;
		sub_task[itask].context = 0;
		sub_task[itask].counter = &sub_counter;
	}

	task_scheduler_multiqueue(task_scheduler, sub_task, sub_task_count);

	memory_deallocate(sub_task);

	task_yield_and_wait(&sub_counter);

	FOUNDATION_ASSERT(atomic_load32(&sub_counter, memory_order_relaxed) == 0);
}

static FOUNDATION_NOINLINE void
task_multi_test(task_context_t context) {
	struct multi_task_setup_t* setup = (struct multi_task_setup_t*)context;
	size_t sub_task_count = setup->sub_task_count;
	task_t* sub_task = memory_allocate(HASH_TEST, sizeof(task_t) * sub_task_count, 0, MEMORY_PERSISTENT);

	atomic32_t sub_counter;
	atomic_store32(&sub_counter, (int32_t)sub_task_count, memory_order_relaxed);
	for (size_t itask = 0; itask < sub_task_count; ++itask) {
		sub_task[itask].function = task_multi_sub_test;
		sub_task[itask].context = (task_context_t)setup;
		sub_task[itask].counter = &sub_counter;
	}

	task_scheduler_multiqueue(task_scheduler, sub_task, sub_task_count);
	task_yield_and_wait(&sub_counter);

	FOUNDATION_ASSERT(atomic_load32(&sub_counter, memory_order_relaxed) == 0);

	atomic_store32(&sub_counter, (int32_t)sub_task_count, memory_order_relaxed);
	memset(sub_task, 0xFF, sizeof(task_t) * sub_task_count);
	for (size_t itask = 0; itask < sub_task_count; ++itask) {
		sub_task[itask].function = task_multi_sub_test;
		sub_task[itask].context = (task_context_t)setup;
		sub_task[itask].counter = &sub_counter;
	}

	task_scheduler_multiqueue(task_scheduler, sub_task, sub_task_count);
	task_yield_and_wait(&sub_counter);

	FOUNDATION_ASSERT(atomic_load32(&sub_counter, memory_order_relaxed) == 0);

	memory_deallocate(sub_task);
}

DECLARE_TEST(task, single) {
	task_scheduler = task_scheduler_allocate(system_hardware_threads(), 128);

	thread_sleep(100);

	task_t task = {0};
	task.function = task_single_test;
	task.counter = &remain_counter;

	atomic_store32(&task_counter, 0, memory_order_relaxed);
	atomic_store32(&remain_counter, 1, memory_order_relaxed);

	task_scheduler_queue(task_scheduler, task);

	task_yield_and_wait(&remain_counter);

	task_scheduler_deallocate(task_scheduler);

	EXPECT_EQ(atomic_load32(&task_counter, memory_order_relaxed), 1);
	EXPECT_EQ(atomic_load32(&remain_counter, memory_order_relaxed), 0);

	return 0;
}

DECLARE_TEST(task, multi) {
	assert_force_continue(false);

	task_scheduler = task_scheduler_allocate(system_hardware_threads(), 1024);

	thread_sleep(100);

	// Six million tasks in total (20*30*5000*2)
	size_t task_count = 20;
	multi_task_setup.sub_task_count = 30;
	multi_task_setup.final_task_count = 5000;
	task_t* task = memory_allocate(HASH_TEST, sizeof(task_t) * task_count, 0, MEMORY_PERSISTENT);
	for (size_t itask = 0; itask < task_count; ++itask) {
		task[itask].function = task_multi_test;
		task[itask].context = (task_context_t)&multi_task_setup;
		task[itask].counter = &remain_counter;
	}

	atomic_store32(&task_counter, 0, memory_order_relaxed);
	atomic_store32(&remain_counter, (int32_t)task_count, memory_order_relaxed);

	task_scheduler_multiqueue(task_scheduler, task, task_count);

	memory_deallocate(task);

	task_yield_and_wait(&remain_counter);

	task_scheduler_deallocate(task_scheduler);

	size_t total_count = task_count * multi_task_setup.sub_task_count * multi_task_setup.final_task_count * 2;
	EXPECT_EQ(atomic_load32(&task_counter, memory_order_relaxed), (int32_t)total_count);
	EXPECT_EQ(atomic_load32(&remain_counter, memory_order_relaxed), 0);

	return 0;
}

static atomic32_t directories_searched;
static atomic32_t files_searched;

static void
task_find_in_file(task_context_t context) {
	char* file_to_search = (char*)context;

	stream_t* stream = stream_open(file_to_search, string_length(file_to_search), STREAM_IN | STREAM_BINARY);
	string_deallocate(file_to_search);

	if (!stream)
		return;

	char buffer[1024];
	while (!stream_eos(stream)) {
		size_t read = stream_read(stream, buffer, sizeof(buffer));
		// memchr
		FOUNDATION_UNUSED(read);
	}

	stream_deallocate(stream);
}

static void
task_find_in_files(task_context_t context) {
	char* path_to_search = (char*)context;
	size_t path_length = string_length(path_to_search);

	/* Files */
	string_t* files = fs_files(path_to_search, path_length);
	uint filecount = array_count(files);
	atomic32_t file_counter;
	atomic_store32(&file_counter, (int32_t)filecount, memory_order_relaxed);
	for (uint ifile = 0; ifile < filecount; ++ifile) {
		task_t subtask = {0};
		subtask.function = task_find_in_file;
		subtask.context =
		    (task_context_t)path_allocate_concat(path_to_search, path_length, STRING_ARGS(files[ifile])).str;
		subtask.counter = &file_counter;
		task_scheduler_queue(task_scheduler, subtask);
	}

	task_yield_and_wait(&file_counter);
	string_array_deallocate(files);

	atomic_add32(&files_searched, (int)filecount, memory_order_relaxed);

	/* Subdirectories */
	string_t* subdirectories = fs_subdirs(path_to_search, path_length);
	uint dircount = array_count(subdirectories);
	atomic32_t subdir_counter;
	atomic_store32(&subdir_counter, (int32_t)dircount, memory_order_relaxed);
	for (uint idir = 0; idir < dircount; ++idir) {
		string_t subpath = path_allocate_concat(path_to_search, path_length, STRING_ARGS(subdirectories[idir]));
		if (task_scheduler->fiber_waiting->node_count > 32) {
			task_find_in_files((task_context_t)subpath.str);
		} else {
			atomic_incr32(&subdir_counter, memory_order_relaxed);

			task_t subtask = {0};
			subtask.function = task_find_in_files;
			subtask.context = (task_context_t)subpath.str;
			subtask.counter = &subdir_counter;
			task_scheduler_queue(task_scheduler, subtask);
		}
	}

	task_yield_and_wait(&subdir_counter);
	string_array_deallocate(subdirectories);

	atomic_add32(&directories_searched, (int)dircount, memory_order_relaxed);

	string_deallocate(path_to_search);
}

DECLARE_TEST(task, find_in_file) {
	task_scheduler = task_scheduler_allocate(system_hardware_threads(), 1024);

	thread_sleep(100);

	atomic32_t counter;
	// string_const_t current_working_dir = environment_current_working_directory();

	task_t task = {0};
	task.function = task_find_in_files;
	task.context = (task_context_t)string_clone(STRING_CONST("D:/") /*STRING_ARGS(current_working_dir)*/).str;
	task.counter = &counter;

	atomic_store32(&counter, 1, memory_order_relaxed);

	task_scheduler_queue(task_scheduler, task);

	task_yield_and_wait(&counter);

	task_scheduler_deallocate(task_scheduler);

	return 0;
}

static void
test_task_declare(void) {
	ADD_TEST(task, single);
	ADD_TEST(task, multi);
	ADD_TEST(task, find_in_file);
}

static test_suite_t test_task_suite = {test_task_application, test_task_memory_system, test_task_config,
                                       test_task_declare,     test_task_initialize,    test_task_finalize};

#if BUILD_MONOLITHIC

int
test_task_run(void);

int
test_task_run(void) {
	test_suite = test_task_suite;
	return test_run_all();
}

#else

test_suite_t
test_suite_define(void);

test_suite_t
test_suite_define(void) {
	return test_task_suite;
}

#endif
