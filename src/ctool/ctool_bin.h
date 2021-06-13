#define MD_DRY 0
#define MD_REAL 1
#define MD_DELE 2
#define MD_LIST 3

struct top;

struct subcontext {
	struct top* top;

	char* path;
	int line;

	int mode;

	char* prefix;
	char* common;
	char* config;
	char* interp;
	char* tooldir;
	char* tool;

	void* rdbuf;
	uint rdsize;
	uint rdlen;

	void* wrbuf;
	uint wrsize;
	uint wrptr;

	void* stmts;
	void* stend;

	void* heap;

	char* args[3];
	uint argn;

	int fd;
};

#define CCT struct subcontext* cct
#define ST struct stmt* st

void fail_syntax(CCT, const char* msg, char* arg) noreturn;

void do_repo(CCT, char* path);
void do_script(CCT, char* dst, char* src);
void do_config(CCT, char* dst, char* src);
void do_link(CCT, char* dst, char* target);

void common_bin_init(CTX, CCT);
void run_statements(CCT, int mode);
void remove_bindir_files(CTX);
