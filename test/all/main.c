/* main.c  -  Foundation test launcher  -  Public Domain  -  2013 Mattias Jansson
 *
 * This library provides a cross-platform foundation library in C11 providing basic support
 * data types and functions to write applications and games in a platform-independent fashion.
 * The latest source code is always available at
 *
 * https://github.com/mjansson/foundation_lib
 *
 * This library is put in the public domain; you can redistribute it and/or modify it without
 * any restrictions.
 */

#include <foundation/foundation.h>
#include <task/task.h>
#include <test/test.h>

static volatile bool test_should_start_flag;
static volatile bool test_have_focus_flag;
static volatile bool test_should_terminate_flag;
static volatile bool test_memory_tracker;

static void*
event_loop(void* arg) {
	event_block_t* block;
	event_t* event = 0;
	FOUNDATION_UNUSED(arg);

	event_stream_set_beacon(system_event_stream(), &thread_self()->beacon);

	while (!test_should_terminate_flag) {
		block = event_stream_process(system_event_stream());
		event = 0;
		while ((event = event_next(block, event))) {
			switch (event->id) {
				case FOUNDATIONEVENT_START:
#if FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID
					log_debug(HASH_TEST, STRING_CONST("Application start event received"));
					test_should_start_flag = true;
#endif
					break;

				case FOUNDATIONEVENT_TERMINATE:
#if FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID
					log_debug(HASH_TEST, STRING_CONST("Application stop/terminate event received"));
					test_should_terminate_flag = true;
					break;
#else
					log_warn(HASH_TEST, WARNING_SUSPICIOUS, STRING_CONST("Terminating tests due to event"));
					process_exit(-2);
#endif

				case FOUNDATIONEVENT_FOCUS_GAIN:
					test_have_focus_flag = true;
					break;

				case FOUNDATIONEVENT_FOCUS_LOST:
					test_have_focus_flag = false;
					break;

				default:
					break;
			}
		}
		thread_wait();
	}

	log_debug(HASH_TEST, STRING_CONST("Application event thread exiting"));

	return 0;
}

#if (FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID) && BUILD_ENABLE_LOG

#if FOUNDATION_PLATFORM_ANDROID
#include <foundation/android.h>
#include <android/native_activity.h>
#endif

#include <foundation/delegate.h>
#include <test/test.h>

static void
test_log_handler(hash_t context, error_level_t severity, const char* msg, size_t length) {
	FOUNDATION_UNUSED(context);
	FOUNDATION_UNUSED(severity);

	if (test_should_terminate_flag)
		return;

#if FOUNDATION_PLATFORM_IOS
	test_text_view_append(delegate_uiwindow(), 1, msg, length);
#elif FOUNDATION_PLATFORM_ANDROID
	jclass test_log_class = 0;
	jmethodID test_log_append = 0;
	const struct JNINativeInterface** jnienv = thread_attach_jvm();
	test_log_class = (*jnienv)->GetObjectClass(jnienv, android_app()->activity->clazz);
	if (test_log_class)
		test_log_append = (*jnienv)->GetMethodID(jnienv, test_log_class, "appendLog", "(Ljava/lang/String;)V");
	if (test_log_append) {
		jstring jstr = (*jnienv)->NewStringUTF(jnienv, msg);
		(*jnienv)->CallVoidMethod(jnienv, android_app()->activity->clazz, test_log_append, jstr);
		(*jnienv)->DeleteLocalRef(jnienv, jstr);
	}
	thread_detach_jvm();
	FOUNDATION_UNUSED(length);
#endif
}

#endif

#if !BUILD_MONOLITHIC

void
test_exception_handler(const char* dump_file, size_t length) {
	FOUNDATION_UNUSED(dump_file);
	FOUNDATION_UNUSED(length);
	log_error(HASH_TEST, ERROR_EXCEPTION, STRING_CONST("Test raised exception"));
	process_exit(-1);
}

#endif

bool
test_should_terminate(void) {
	return test_should_terminate_flag;
}

int
main_initialize(void) {
	foundation_config_t config;
	application_t application;
	int ret;
	size_t iarg, asize;
	const string_const_t* cmdline = environment_command_line();

	test_memory_tracker = true;
	for (iarg = 0, asize = array_size(cmdline); iarg < asize; ++iarg) {
		if (string_equal(STRING_ARGS(cmdline[iarg]), STRING_CONST("--no-memory-tracker")))
			test_memory_tracker = false;
	}

	if (test_memory_tracker)
		memory_set_tracker(memory_tracker_local());

	memset(&config, 0, sizeof(config));

	memset(&application, 0, sizeof(application));
	application.name = string_const(STRING_CONST("Task library test suite"));
	application.short_name = string_const(STRING_CONST("test_all"));
	application.company = string_const(STRING_CONST(""));
	application.version = task_module_version();
	application.flags = APPLICATION_UTILITY;
	application.exception_handler = test_exception_handler;

	log_set_suppress(0, ERRORLEVEL_INFO);

#if (FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID) && BUILD_ENABLE_LOG
	log_set_handler(test_log_handler);
#endif

#if !FOUNDATION_PLATFORM_IOS && !FOUNDATION_PLATFORM_ANDROID

	test_should_start_flag = true;

#endif

	ret = foundation_initialize(memory_system_malloc(), application, config);

#if BUILD_MONOLITHIC
	if (ret == 0) {
		task_config_t task_config;
		memset(&task_config, 0, sizeof(task_config));
		ret = task_module_initialize(task_config);

		test_set_suitable_working_directory();
	}
#endif
	return ret;
}

#if FOUNDATION_PLATFORM_ANDROID
#include <foundation/android.h>
#endif

#if BUILD_MONOLITHIC
extern int
test_task_run(void);
typedef int (*test_run_fn)(void);

static void*
test_runner(void* arg) {
	test_run_fn* tests = (test_run_fn*)arg;
	int test_fn = 0;
	int process_result = 0;

	while (tests[test_fn] && (process_result >= 0)) {
		if ((process_result = tests[test_fn]()) >= 0)
			log_infof(HASH_TEST, STRING_CONST("All tests passed (%d)"), process_result);
		++test_fn;
	}

	return (void*)(intptr_t)process_result;
}

#endif

int
main_run(void* main_arg) {
#if !BUILD_MONOLITHIC
	string_const_t pattern;
	string_t* exe_paths = 0;
	size_t iexe, exesize;
	process_t* process = 0;
	string_t process_path = {0, 0};
	unsigned int* exe_flags = 0;
#else
	void* test_result;
#endif
#if FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID
	int remain_counter = 0;
#endif
#if BUILD_DEBUG
	const string_const_t build_name = string_const(STRING_CONST("debug"));
#elif BUILD_RELEASE
	const string_const_t build_name = string_const(STRING_CONST("release"));
#elif BUILD_PROFILE
	const string_const_t build_name = string_const(STRING_CONST("profile"));
#elif BUILD_DEPLOY
	const string_const_t build_name = string_const(STRING_CONST("deploy"));
#endif
#if BUILD_MONOLITHIC
	const string_const_t build_type = string_const(STRING_CONST(" monolithic"));
#else
	const string_const_t build_type = string_empty();
#endif
	char* pathbuf;
	int process_result = 0;
	thread_t event_thread;
	FOUNDATION_UNUSED(main_arg);
	FOUNDATION_UNUSED(build_name);

	log_set_suppress(HASH_TEST, ERRORLEVEL_DEBUG);

	log_infof(HASH_TEST, STRING_CONST("Task library v%s built for %s using %s (%.*s%.*s)"),
	          string_from_version_static(task_module_version()).str, FOUNDATION_PLATFORM_DESCRIPTION,
	          FOUNDATION_COMPILER_DESCRIPTION, STRING_FORMAT(build_name), STRING_FORMAT(build_type));

	thread_initialize(&event_thread, event_loop, 0, STRING_CONST("event_thread"), THREAD_PRIORITY_NORMAL, 0);
	thread_start(&event_thread);

	pathbuf = memory_allocate(HASH_STRING, BUILD_MAX_PATHLEN, 0, MEMORY_PERSISTENT);

	while (!thread_is_running(&event_thread))
		thread_sleep(10);

#if FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID
	while (!test_should_start_flag) {
#if FOUNDATION_PLATFORM_ANDROID
		system_process_events();
#endif
		thread_sleep(100);
	}
#endif

	fs_remove_directory(STRING_ARGS(environment_temporary_directory()));

#if BUILD_MONOLITHIC

	test_run_fn tests[] = {test_task_run, 0};

#if FOUNDATION_PLATFORM_ANDROID

	thread_t test_thread;
	thread_initialize(&test_thread, test_runner, tests, STRING_CONST("test_runner"), THREAD_PRIORITY_NORMAL, 0);
	thread_start(&test_thread);

	log_debug(HASH_TEST, STRING_CONST("Starting test runner thread"));

	while (!thread_is_running(&test_thread)) {
		system_process_events();
		thread_sleep(10);
	}

	while (thread_is_running(&test_thread)) {
		system_process_events();
		thread_sleep(10);
	}

	test_result = thread_join(&test_thread);
	process_result = (int)(intptr_t)test_result;

	thread_finalize(&test_thread);

#else

	test_result = test_runner(tests);
	process_result = (int)(intptr_t)test_result;

#endif

	if (process_result != 0)
		log_warnf(HASH_TEST, WARNING_SUSPICIOUS, STRING_CONST("Tests failed with exit code %d"), process_result);

#if FOUNDATION_PLATFORM_IOS || FOUNDATION_PLATFORM_ANDROID

	while (!test_should_terminate() && test_have_focus() && (remain_counter < 50)) {
		system_process_events();
		thread_sleep(100);
		++remain_counter;
	}

#endif

	log_debug(HASH_TEST, STRING_CONST("Exiting main loop"));

#else  // !BUILD_MONOLITHIC

	// Find all test executables in the current executable directory
#if FOUNDATION_PLATFORM_WINDOWS
	pattern = string_const(STRING_CONST("^test-.*\\.exe$"));
#elif FOUNDATION_PLATFORM_MACOS
	pattern = string_const(STRING_CONST("^test-.*$"));
#elif FOUNDATION_PLATFORM_POSIX
	pattern = string_const(STRING_CONST("^test-.*$"));
#else
#error Not implemented
#endif
	exe_paths = fs_matching_files(STRING_ARGS(environment_executable_directory()), STRING_ARGS(pattern), false);
	array_resize(exe_flags, array_size(exe_paths));
	memset(exe_flags, 0, sizeof(unsigned int) * array_size(exe_flags));
#if FOUNDATION_PLATFORM_MACOS
	// Also search for test applications
	string_const_t app_pattern = string_const(STRING_CONST("^test-.*\\.app$"));
	regex_t* app_regex = regex_compile(app_pattern.str, app_pattern.length);
	string_t* subdirs = fs_subdirs(STRING_ARGS(environment_executable_directory()));
	for (size_t idir = 0, dirsize = array_size(subdirs); idir < dirsize; ++idir) {
		if (regex_match(app_regex, subdirs[idir].str, subdirs[idir].length, 0, 0)) {
			string_t exe_path = {subdirs[idir].str, subdirs[idir].length - 4};
			array_push(exe_paths, exe_path);
			array_push(exe_flags, PROCESS_MACOS_USE_OPENAPPLICATION);
		}
	}
	string_array_deallocate(subdirs);
	regex_deallocate(app_regex);
#endif
	for (iexe = 0, exesize = array_size(exe_paths); iexe < exesize; ++iexe) {
		string_const_t* process_args = 0;
		string_const_t exe_file_name = path_base_file_name(STRING_ARGS(exe_paths[iexe]));
		if (string_equal(STRING_ARGS(exe_file_name), STRING_ARGS(environment_executable_name())))
			continue;  // Don't run self

		process_path = path_concat(pathbuf, BUILD_MAX_PATHLEN, STRING_ARGS(environment_executable_directory()),
		                           STRING_ARGS(exe_paths[iexe]));
		process = process_allocate();

		process_set_executable_path(process, STRING_ARGS(process_path));
		process_set_working_directory(process, STRING_ARGS(environment_executable_directory()));
		process_set_flags(process, PROCESS_ATTACHED | exe_flags[iexe]);

		if (!test_memory_tracker)
			array_push(process_args, string_const(STRING_CONST("--no-memory-tracker")));
		process_set_arguments(process, process_args, array_size(process_args));

		log_infof(HASH_TEST, STRING_CONST("Running test executable: %.*s"), STRING_FORMAT(exe_paths[iexe]));

		process_result = process_spawn(process);
		while (process_result == PROCESS_WAIT_INTERRUPTED) {
			thread_sleep(10);
			process_result = process_wait(process);
		}
		process_deallocate(process);
		array_deallocate(process_args);

		if (process_result != 0) {
			if (process_result >= PROCESS_INVALID_ARGS)
				log_warnf(HASH_TEST, WARNING_SUSPICIOUS, STRING_CONST("Tests failed, process terminated with error %x"),
				          process_result);
			else
				log_warnf(HASH_TEST, WARNING_SUSPICIOUS, STRING_CONST("Tests failed with exit code %d"),
				          process_result);
			process_set_exit_code(-1);
			goto exit;
		}

		log_infof(HASH_TEST, STRING_CONST("All tests from %.*s passed (%d)"), STRING_FORMAT(exe_paths[iexe]),
		          process_result);
	}

	log_info(HASH_TEST, STRING_CONST("All tests passed"));

exit:

	if (exe_paths)
		string_array_deallocate(exe_paths);
	array_deallocate(exe_flags);

#endif

	test_should_terminate_flag = true;

	thread_signal(&event_thread);
	thread_finalize(&event_thread);

	memory_deallocate(pathbuf);

	log_infof(HASH_TEST, STRING_CONST("Tests exiting: %s (%d)"), process_result ? "FAILED" : "PASSED", process_result);

	if (process_result)
		memory_set_tracker(memory_tracker_none());

	return process_result;
}

void
main_finalize(void) {
#if FOUNDATION_PLATFORM_ANDROID
	thread_detach_jvm();
#endif

	foundation_finalize();
}
