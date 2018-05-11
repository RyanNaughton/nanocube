#include "../base/platform.c"

#include "../app.h"

#include "nix_platform.c"
#include <signal.h>

#if defined(OS_MAC)
#include <mach-o/dyld.h>
#endif

global_variable ApplicationState app_state;

void exit_callback(void)
{
	//gs_nanodb.deregister_nanocube(exit_status);
}

void signal_handler(int signum)
{
	if (signum == SIGINT) {
		app_state.interrupted = 1;
		nix_app_state_interrupted = 1;
	} else {
		exit(signum);
	}
}

void setup_signal_handlers()
{
	// If SIGHUP was being ignored, keep it that way...
	void (*previous_signal_handler)(int);

	previous_signal_handler = signal(SIGHUP, signal_handler);
	if (previous_signal_handler == SIG_IGN) {
		signal(SIGHUP, SIG_IGN);
	}
	signal(SIGINT,  signal_handler);
	signal(SIGQUIT, signal_handler);
	signal(SIGILL,  signal_handler);
	signal(SIGTRAP, signal_handler);
	signal(SIGABRT, signal_handler);
	signal(SIGFPE,  signal_handler);
	signal(SIGSEGV, signal_handler);
	signal(SIGTERM, signal_handler);
	atexit(exit_callback);
}

typedef struct {
	void*                      application_code_dll;
	ApplicationProcessRequest* application_process_request;
	b8                         is_valid;
} NIXApplicationCode;

internal NIXApplicationCode
nix_load_application_code(char *application_dll_path, Print *print)
{
	NIXApplicationCode result;
	result.application_code_dll = dlopen(application_dll_path, RTLD_NOW); // RTLD_LAZY);

	// Assert(result.application_process_request);
	if (!result.application_code_dll)
	{
		Print_cstr(print, dlerror());
		Print_cstr(print, "\n");
		result.is_valid = 0;
		return result;
	}

	result.application_process_request = (ApplicationProcessRequest*) dlsym(result.application_code_dll, "application_process_request");

	// Assert(result.application_process_request);
	result.is_valid = result.application_process_request != 0;

	return result;
}

internal
PLATFORM_WORK_QUEUE_CALLBACK(test_worker_task)
{
	// u64 tid;
	// pthread_threadid_np(0, &tid);
	// mach_port_t tid = pthread_mach_thread_np(pthread_self());
	// pthread_threadid_np(0, &tid);
	// pid_t tid = gettid();// pthread_id_np_t tid;
	// tid = pthread_getthreadid_np();
	printf("Thread %p: %s\n", (void*) pthread_self(), (char*) data);
	usleep(1000000);
}

//------------------------------------------------------------------------------
// main
//------------------------------------------------------------------------------

#ifdef PROFILE
global_variable pf_Table global_profile_table_storage = { .slot_event_index = 0 };
pf_Table *global_profile_table = &global_profile_table_storage;
#endif

#define main_BUFFER_SIZE Megabytes(1)

static Print*
print_new(u64 buffer_size)
{
	u64 size = RALIGN(buffer_size, 4096);
	char *buffer = (char*) malloc(size);
	Assert(buffer);
	Print *print = (Print*) buffer;
	Print_init(print, buffer + sizeof(Print), buffer + size);
	return print;
}

static void
print_delete(Print *print)
{
	free(print);
}

int
main(int num_args, char** args)
{
	app_state.data        = 0;
	app_state.interrupted = 0;

#ifdef PROFILE
	/* initialize profile table */
	u64 profile_memory_size = Megabytes(128);
	pt_Memory profile_memory = nix_allocate_memory(profile_memory_size, 3, 0);
	pf_Table_init(global_profile_table, profile_memory.memblock.begin, profile_memory.memblock.end);
	app_state.global_profile_table = (void*) global_profile_table;
#endif

	/* signal handlers */
	setup_signal_handlers();

	/* make sure any initial state needed by the linux platform is initializes */
	nix_init();

	/* initialize a PlatformAPI object by lining specific functions */
	nix_init_platform(&app_state.platform);

	// prepare platform file handle
	pt_File platform_stdout;
	platform_stdout.open = 1;
	platform_stdout.eof = 0;
	platform_stdout.write = 1;
	platform_stdout.read = 0;
	platform_stdout.last_seek_success = 0;
	platform_stdout.last_read = 0;
	platform_stdout.handle = stdout;

	// prepare platform file handle
	pt_File platform_stdin;
	platform_stdin.open = 1;
	platform_stdin.eof = 0;
	platform_stdin.write = 0;
	platform_stdin.read = 1;
	platform_stdin.last_seek_success = 0;
	platform_stdin.last_read = 0;
	platform_stdin.handle = stdin;

	// buffer
	Print *print               = print_new(main_BUFFER_SIZE);
	Print *print_dlopen_issues = print_new(Kilobytes(32));

	// get .dll full path
	FilePath executable_path;
	app_state.platform.executable_path(&executable_path);

	//
	char *path = print->begin;
	Print_str(print, executable_path.full_path, executable_path.name);
	char *path_end = print->end;

// typedef struct {
// 	char  full_path[MAX_FILE_PATH_SIZE+1];
// 	char* name;
// 	char* extension; // extension start last . in name
// 	char* end;
// } FilePath;



	char *lib_names[] = { "libnanocube_app.so", "libnanocube_app.dylib", "../lib/libnanocube_app.so", "../lib/libnanocube_app.dylib" };

	NIXApplicationCode app_code = { 0 };
	for (s32 i=0; i<ArrayCount(lib_names); ++i) {
		print->end = path_end;
		Print_cstr(print,lib_names[i]);
		Print_char(print,0);
		NIXApplicationCode current_app_code = nix_load_application_code(path, print_dlopen_issues);
		if (current_app_code.is_valid) {
			app_code = current_app_code;
			break;
		}
	}

	if (app_code.application_code_dll == 0) {
		app_state.platform.write_to_file(&platform_stdout, print_dlopen_issues->begin, print_dlopen_issues->end);
		print_delete(print);
		print_delete(print_dlopen_issues);
		fputs("[Problem] Couldn't load dynamic library through any of its expected names (ie. libnanocube_app.so or libnanocube_app.dylib)\n",stderr);
		return -1;
	} else {
		print_delete(print_dlopen_issues);
	}

	//
	// treat request as a memory block by copying all
	// the arguments into a single buffer space separated
	//
	Print_clear(print);
	for (s64 i=1;i<num_args;++i)
	{
		if (i > 1)
			Print_char(print, ' ');
		Print_cstr(print, args[i]);
	}

#if 0
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A0");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A1");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A2");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A3");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A4");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A5");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A6");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A7");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A8");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load A9");
	app_state.platform.work_queue_complete_work(app_state.work_queue);

	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B0");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B1");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B2");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B3");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B4");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B5");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B6");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B7");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B8");
	app_state.platform.work_queue_add_entry(app_state.work_queue,test_worker_task,"Load B9");
	app_state.platform.work_queue_complete_work(app_state.work_queue);
#endif

	app_code.application_process_request(&app_state, print->begin, print->end, &platform_stdin, &platform_stdout);

#if 0
	/* destroy semaphore */
	pt_WorkQueue_free(app_state.work_queue);
#endif

#ifdef PROFILE
	nix_free_memory(&profile_memory);
#endif

	print_delete(print);

	return 0;

}

