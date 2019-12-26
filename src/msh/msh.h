#include <cdefs.h>

#define EV_SIZE 0xFFFF

#define EV_ENVP (1<<16)
#define EV_SVAR (1<<17)
#define EV_TRAP (1<<18)

#define EV_REF  (1<<24)

/* env entry types */

struct mbuf {
	char* buf;
	int len;
};

/* Heap layout, at the point when cmd_exec() calls execve():

   heap                                argv                hend
   v                                   v                   v
   Env Env Env Env Env Arg Arg Arg Arg ARGV ENVP ..........
                       ^                         ^
                       asep                      hptr

   Ep = struct env
   Arg = raw 0-terminated string
   ARGV = char* argv[] pointing back to Arg-s
   ENVP = char* envp[] pointing back to Env-s and/or following Env references.

   Until the first env change, ctx->customenvp is negative and ctx->environ
   is passed directly to execve(). */

struct sh {
	char* file;      /* for error reporting */
	int line;

	int errfd;       /* stderr, dup'ed if necessary */
	int sigfd;       /* see cmd_run() */

	char** environ;  /* original, as passed to msh itself */
	int customenvp;  /* whether we need to go through env's */

	int topargc;     /* originals */
	int topargp;
	char** topargv;

	int state;       /* of the parser */

	int argc;        /* the command being parsed */
	int argp;
	char** argv;

	char* heap;      /* see layout scheme above */
	char* asep;
	char* hptr;
	char* hend;
	char* var;       /* heap ptr to $var being substituted */

	struct mbuf passwd;
	struct mbuf groups;
};

struct env {
	unsigned key;
	char payload[];
};

#define CTX struct sh* ctx __unused

void heap_init(CTX);
void* heap_alloc(CTX, int len);
void heap_extend(CTX);
void hrev(CTX, int type);
void hset(CTX, int what);

void parse(CTX, char* buf, int len);
void parse_finish(CTX);

void exit(CTX, int code) noreturn;
void quit(CTX, const char* err, char* arg, int ret) noreturn;
void error(CTX, const char* err, char* arg, int ret) noreturn;
void fatal(CTX, const char* err, char* arg) noreturn;

char* shift(CTX);
char* next(CTX);
void shift_int(CTX, int* dst);
void shift_u64(CTX, uint64_t* dst);
void shift_oct(CTX, int* dst);
void no_more_arguments(CTX);
int got_more_arguments(CTX);
void need_some_arguments(CTX);
void check(CTX, const char* msg, char* arg, int ret);
char** argsleft(CTX);
char* dash_opts(CTX);

int mmapfile(struct mbuf* mb, char* name);
int munmapfile(struct mbuf* mb);

void map_file(CTX, struct mbuf* mb, char* name);

int get_user_id(CTX, char* user);
int get_group_id(CTX, char* group);

struct env* env_first(CTX);
struct env* env_next(CTX, struct env* at);
char* env_value(CTX, struct env* at, int type);
