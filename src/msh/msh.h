#include <bits/ints.h>

/* arguments for hset/hrev */
#define HEAP 0
#define ESEP 1
#define CSEP 2
#define VSEP 3

/* env entry types */
#define ENVDEL 0
#define ENVPTR 1
#define ENVSTR 2
#define ENVLOC 3

/* Heap layout, at the point when end_cmd() calls exec():

   heap                csep                           hend
   v                   v                              v
   Ep Ep Ep Es Es ENVP Arg Arg Arg Arg ARGV ..........|
                  ^                         ^
                  esep                      hptr

   Ep = struct envptr
   Es = struct env with inline payload
   ENVP = char** envp pointing back to Es-s and/or following Ep-s
   Arg = raw 0-terminated string
   ARGV = char** argv pointing back to Arg-s

   Until the first env change, esep=NULL, csep=heap and sh.envp
   points to the original main() argument. */

struct sh {
	char* file;      /* for error reporting */
	int line;
	int errfd;       /* stderr, dup'ed if necessary */

	char** envp;

	int topargc;     /* for the script itself */
	int topargp;
	char** topargv;

	int state;       /* of the parser */
	int argc;
	char** argv;     /* the command being parsed */
	int argp;

	char* heap;      /* layout scheme above */
	char* esep;
	char* csep;
	char* hptr;
	char* hend;
	char* var;       /* heap ptr to $var being substituted */

	int dash;        /* leading - to suppress abort-on-failure */

	char pid[20];

	char trap[50];   /* see cmd_trap() */
};

struct env {
	unsigned short len;
	char type;
	char payload[];
};

struct envptr {
	unsigned short len;
	char type;
	char* ref;
};

struct mbuf {
	char* buf;
	int len;
};

void hinit(struct sh* ctx);
void* halloc(struct sh* ctx, int len);
void hrev(struct sh* ctx, int type);
void hset(struct sh* ctx, int what);

void parse(struct sh* ctx, char* buf, int len);
void pfini(struct sh* ctx);

void loadvar(struct sh* ctx, char* var);
char* valueof(struct sh* ctx, char* var);
void setenv(struct sh* ctx, char* pkey, char* pval);
void define(struct sh* ctx, char* var, char* val);
void undef(struct sh* ctx, char* var);
int export(struct sh* ctx, char* var);

void command(struct sh* ctx);

#define NR __attribute__((noreturn))
void quit(struct sh* ctx, const char* err, char* arg, long ret) NR;
int error(struct sh* ctx, const char* err, char* arg, long ret);
void fatal(struct sh* ctx, const char* err, char* arg) NR;
int fchk(long ret, struct sh* ctx, char* arg);

int numleft(struct sh* ctx);
int dasharg(struct sh* ctx);
int moreleft(struct sh* ctx);
int noneleft(struct sh* ctx);
char** argsleft(struct sh* ctx);
char* peek(struct sh* ctx);
char* shift(struct sh* ctx);
int shift_str(struct sh* ctx, char** dst);
int shift_int(struct sh* ctx, int* dst);
int shift_u64(struct sh* ctx, uint64_t* dst);
int shift_oct(struct sh* ctx, int* dst);

int mmapfile(struct mbuf* mb, char* name);
int munmapfile(struct mbuf* mb);
