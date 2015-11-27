/* main.c  -  Task test for task library  -  MIT License  -  2014 Mattias Jansson / Rampant Pixels
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
	app.config_dir = string_const(STRING_CONST("test_task"));
	app.flags = APPLICATION_UTILITY;
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
	log_set_suppress(HASH_TASK, ERRORLEVEL_INFO);
	memset(&config, 0, sizeof(config));
	return task_module_initialize(config);
}

static void
test_task_finalize(void) {
	task_module_finalize();
}

static atomic32_t _task_counter;

static task_return_t
task_test(task_arg_t arg) {
	log_info(HASH_TASK, STRING_CONST("Task executing"));
	atomic_incr32(&_task_counter);
	return task_return(TASK_FINISH, 0);
}

static task_return_t
task_yield(task_arg_t arg) {
	int* valuearg = arg;
	if (*valuearg) {
		log_info(HASH_TASK, STRING_CONST("Yield task finishing"));
		atomic_incr32(&_task_counter);
		return task_return(TASK_FINISH, 0);
	}
	log_info(HASH_TASK, STRING_CONST("Yield task yielding"));
	(*valuearg)++;
	return task_return(TASK_YIELD, random32_range(20, 100) * (int)(time_ticks_per_second() / 1000));
}

static task_return_t
task_load(task_arg_t arg) {
	int i;
	FOUNDATION_UNUSED(arg);
	for (i = 0; i < 1024 * 8; ++i) {
		if (random_range(0, 1) > 1)
			break;
	}
	atomic_incr32(&_task_counter);
	return task_return(TASK_FINISH, 0);
}

DECLARE_TEST(task, single) {
	task_scheduler_t* scheduler = task_scheduler_allocate(8, 128);
	task_t task = {task_test, 0, string_const(STRING_CONST("single_task"))};

	atomic_store32(&_task_counter, 0);

	task_scheduler_set_executor_count(scheduler, 4);
	task_scheduler_start(scheduler);

	thread_sleep(100);
	task_scheduler_queue(scheduler, task, 0, 0);
	thread_sleep(100);

	EXPECT_EQ(atomic_load32(&_task_counter), 1);

	task_scheduler_stop(scheduler);

	task_scheduler_queue(scheduler, task, 0, 0);
	task_scheduler_start(scheduler);

	thread_sleep(1000);

	EXPECT_EQ(atomic_load32(&_task_counter), 2);

	task_scheduler_stop(scheduler);

	task_scheduler_queue(scheduler, task, 0, 0);
	task_scheduler_step(scheduler, 0);

	EXPECT_EQ(atomic_load32(&_task_counter), 3);

	task_scheduler_queue(scheduler, task, 0, time_current() + time_ticks_per_second());
	task_scheduler_step(scheduler, 0);

	EXPECT_EQ(atomic_load32(&_task_counter), 3);

	thread_sleep(10);
	task_scheduler_step(scheduler, 0);

	EXPECT_EQ(atomic_load32(&_task_counter), 3);

	thread_sleep(1000);
	task_scheduler_step(scheduler, 0);

	EXPECT_EQ(atomic_load32(&_task_counter), 4);

	task_scheduler_start(scheduler);
	task_scheduler_queue(scheduler, task, 0, time_current() + time_ticks_per_second());

	thread_sleep(1100);

	EXPECT_EQ(atomic_load32(&_task_counter), 5);

	task_scheduler_queue(scheduler, task, 0,
	                     time_current() + time_ticks_per_second() * 5);
	task_scheduler_deallocate(scheduler);

	EXPECT_EQ(atomic_load32(&_task_counter), 5);

	return 0;
}

DECLARE_TEST(task, multiple) {
	task_scheduler_t* scheduler = task_scheduler_allocate(system_hardware_threads(), 1024);
	task_t task[4] = {
		{task_test, 0, string_const(STRING_CONST("first_task"))},
		{task_test, 0, string_const(STRING_CONST("second_task"))},
		{task_test, 0, string_const(STRING_CONST("third_task"))},
		{task_test, 0, string_const(STRING_CONST("fourth_task"))}
	};

	atomic_store32(&_task_counter, 0);

	task_scheduler_start(scheduler);

	thread_sleep(100);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	task_scheduler_queue(scheduler, task[0], 0, 0);
	task_scheduler_queue(scheduler, task[1], 0, 0);
	task_scheduler_queue(scheduler, task[2], 0, 0);
	task_scheduler_queue(scheduler, task[3], 0, 0);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	thread_sleep(100);

	EXPECT_EQ(atomic_load32(&_task_counter), 16);

	task_scheduler_stop(scheduler);
	EXPECT_EQ(atomic_load32(&_task_counter), 16);
	atomic_store32(&_task_counter, 0);

	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	task_scheduler_queue(scheduler, task[0], 0, 0);
	task_scheduler_queue(scheduler, task[1], 0, 0);
	task_scheduler_queue(scheduler, task[2], 0, 0);
	task_scheduler_queue(scheduler, task[3], 0, 0);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);

	task_scheduler_start(scheduler);
	thread_sleep(100);

	EXPECT_EQ(atomic_load32(&_task_counter), 16);

	task_scheduler_stop(scheduler);
	EXPECT_EQ(atomic_load32(&_task_counter), 16);
	atomic_store32(&_task_counter, 0);

	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	task_scheduler_queue(scheduler, task[0], 0, 0);
	task_scheduler_queue(scheduler, task[1], 0, 0);
	task_scheduler_queue(scheduler, task[2], 0, 0);
	task_scheduler_queue(scheduler, task[3], 0, 0);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
	task_scheduler_multiqueue(scheduler, 4, task, 0, 0);

	task_scheduler_step(scheduler, 500);
	EXPECT_EQ(atomic_load32(&_task_counter), 16);

	task_scheduler_deallocate(scheduler);

	return 0;
}

DECLARE_TEST(task, yield) {
	task_scheduler_t* scheduler = task_scheduler_allocate(
	                                  system_hardware_threads(), 1024);
	task_t task = {task_yield, 0, string_const(STRING_CONST("yield_task"))};
	int arg = 0;

	task_t multitask[8] = { task, task, task, task, task, task, task, task };
	int multiarg[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	void* multiargptr[8] = { &multiarg[0], &multiarg[1], &multiarg[2], &multiarg[3], &multiarg[4], &multiarg[5], &multiarg[6], &multiarg[7] };

	atomic_store32(&_task_counter, 0);

	task_scheduler_start(scheduler);

	thread_sleep(100);
	task_scheduler_queue(scheduler, task, &arg, 0);
	thread_sleep(10);

	EXPECT_EQ(atomic_load32(&_task_counter), 0);

	task_scheduler_multiqueue(scheduler, 8, multitask, multiargptr, 0);

	EXPECT_EQ(atomic_load32(&_task_counter), 0);

	thread_sleep(1000);

	EXPECT_EQ(atomic_load32(&_task_counter), 9);

	task_scheduler_deallocate(scheduler);

	return 0;
}

static void*
producer_thread(void* arg) {
	int i;
	task_scheduler_t* scheduler = arg;
	task_t task[4] = {
		{task_load, 0, string_const(STRING_CONST("first_load"))},
		{task_load, 0, string_const(STRING_CONST("second_load"))},
		{task_load, 0, string_const(STRING_CONST("third_load"))},
		{task_load, 0, string_const(STRING_CONST("fourth_load"))}
	};

	for (i = 0; i < 100; ++i) {
		task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
		task_scheduler_queue(scheduler, task[0], 0, 0);
		task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
		task_scheduler_queue(scheduler, task[1], 0, 0);
		task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
		task_scheduler_queue(scheduler, task[2], 0, 0);
		task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
		task_scheduler_queue(scheduler, task[3], 0, 0);
		task_scheduler_multiqueue(scheduler, 4, task, 0, 0);
		thread_yield();
	}

	return 0;
}

DECLARE_TEST(task, load) {
	size_t i;
	thread_t thread[32];
	task_scheduler_t* scheduler;
	task_statistics_t stats;
	size_t num_executors = system_hardware_threads() - 1;
	size_t num_producers = system_hardware_threads() - 1;
    
    num_executors = math_clamp(num_executors, 0, 32);
    num_producers = math_clamp(num_producers, 2, 32);

	atomic_store32(&_task_counter, 0);

	scheduler = task_scheduler_allocate(num_executors, 100 * 30 * (int)num_producers);
	task_scheduler_start(scheduler);

	for (i = 0; i < num_producers; ++i) {
		thread_initialize(&thread[i], producer_thread, scheduler,
		                  STRING_CONST("task_producer"), THREAD_PRIORITY_NORMAL, 0);
		thread_start(&thread[i]);
	}

	test_wait_for_threads_startup(thread, num_producers);

	for (i = 0; i < num_producers; ++i)
		thread_finalize(&thread[i]);

	while (!task_scheduler_is_idle(scheduler))
		task_scheduler_step(scheduler, -1);

	stats = task_scheduler_statistics(scheduler);
	task_scheduler_deallocate(scheduler);

	EXPECT_INTEQ(atomic_load32(&_task_counter), 100 * 24 * (int)num_producers);
#if BUILD_TASK_ENABLE_STATISTICS
	EXPECT_EQ(stats.num_executed, 100 * 24 * (int)num_producers);
#else
	EXPECT_EQ(stats.num_executed, 0);
#endif

	return 0;
}

static void
test_task_declare(void) {
	ADD_TEST(task, single);
	ADD_TEST(task, multiple);
	ADD_TEST(task, yield);
	ADD_TEST(task, load);
}

test_suite_t test_task_suite = {
	test_task_application,
	test_task_memory_system,
	test_task_config,
	test_task_declare,
	test_task_initialize,
	test_task_finalize
};

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
