typedef struct {
	void            *data;
	pt_WorkQueue    *work_queue;
	PlatformAPI     platform;
	b8              interrupted;
	void            *global_profile_table;

#ifdef POLYCOVER
	PolycoverAPI    polycover;
#endif

} ApplicationState;

#define APPLICATION_PROCESS_REQUEST(name) \
	void name(ApplicationState *app_state, \
		  char *request_begin, \
		  char *request_end, \
		  pt_File *pfh_stdin, \
		  pt_File *pfh_stdout, \
		  pt_File *pfh_stderr)
typedef APPLICATION_PROCESS_REQUEST(ApplicationProcessRequest);

