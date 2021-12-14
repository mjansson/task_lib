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
	task_scheduler = task_scheduler_allocate(1 /*system_hardware_threads()*/, 128);

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
static atomic32_t files_found;
static atomic32_t file_remain_counter;

static const char keyword[] = "main";

static void
task_find_in_file(task_context_t context) {
	char* file_to_search = (char*)context;

	stream_t* stream = stream_open(file_to_search, string_length(file_to_search), STREAM_IN | STREAM_BINARY);
	string_deallocate(file_to_search);

	if (!stream)
		return;

	error_context_push(STRING_CONST("Find in file"), STRING_ARGS(stream->path));

	bool keyword_found = false;
	size_t buffer_size = 60 * 1024;
	char* buffer = memory_allocate(0, buffer_size, 0, MEMORY_PERSISTENT);
	while (!keyword_found && !stream_eos(stream)) {
		size_t read = stream_read(stream, buffer, buffer_size);

		void* current = buffer;
		size_t remain = read;
		while (remain) {
			void* found = memchr(current, keyword[0], remain);
			if (!found)
				break;

			size_t offset = (size_t)pointer_diff(found, current);
			remain -= offset;
			if (remain < sizeof(keyword)) {
				if (!stream_eos(stream))
					stream_seek(stream, (ssize_t)sizeof(keyword) - (ssize_t)remain, STREAM_SEEK_CURRENT);
				break;
			}

			current = found;
			if (string_equal(current, sizeof(keyword), keyword, sizeof(keyword))) {
				atomic_incr32(&files_found, memory_order_relaxed);
				keyword_found = true;
				break;
			}
			current = pointer_offset(current, 1);
			--remain;
		}
	}

	memory_deallocate(buffer);

	error_context_pop();

	stream_deallocate(stream);

	atomic_incr32(&files_searched, memory_order_relaxed);
}

static void
task_find_in_files(string_const_t path) {
	error_context_push(STRING_CONST("Find in files"), STRING_ARGS(path));

	/* Files */
	string_t* files = fs_files(STRING_ARGS(path));
	uint filecount = array_count(files);
	atomic_add32(&file_remain_counter, (int32_t)filecount, memory_order_relaxed);
	for (uint ifile = 0; ifile < filecount; ++ifile) {
		task_t subtask = {0};
		subtask.function = task_find_in_file;
		subtask.context = (task_context_t)path_allocate_concat(STRING_ARGS(path), STRING_ARGS(files[ifile])).str;
		subtask.counter = &file_remain_counter;
		task_scheduler_queue(task_scheduler, subtask);
	}

	string_array_deallocate(files);

	/* Subdirectories */
	string_t* subdirectories = fs_subdirs(STRING_ARGS(path));
	uint dircount = array_count(subdirectories);
	for (uint idir = 0; idir < dircount; ++idir) {
		string_t subpath = path_allocate_concat(STRING_ARGS(path), STRING_ARGS(subdirectories[idir]));
		task_find_in_files(string_const(STRING_ARGS(subpath)));
		string_deallocate(subpath.str);
		atomic_incr32(&directories_searched, memory_order_relaxed);
	}

	string_array_deallocate(subdirectories);

	error_context_pop();
}

DECLARE_TEST(task, find_in_file) {
	task_scheduler = task_scheduler_allocate(system_hardware_threads(), 4096);

	atomic_store32(&file_remain_counter, 0, memory_order_relaxed);

	task_find_in_files(environment_current_working_directory());

	task_yield_and_wait(&file_remain_counter);

	task_scheduler_deallocate(task_scheduler);

	error_level_t suppress = log_suppress(HASH_TEST);
	log_set_suppress(HASH_TEST, ERRORLEVEL_DEBUG);
	log_infof(HASH_TEST, STRING_CONST("Searched %d files and %d directories, found %d matching files"),
	          atomic_load32(&files_searched, memory_order_relaxed),
	          atomic_load32(&directories_searched, memory_order_relaxed),
	          atomic_load32(&files_found, memory_order_relaxed));
	log_set_suppress(HASH_TEST, suppress);

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
