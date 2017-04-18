#include <bits/ints.h>

/* arguments for hset/hrev */
#define HEAP 0
#define ESEP 1
#define CSEP 2
#define VSEP 3

/* env entry types */
#define ENVDEL 0
#define ENVSTR 1
#define ENVPTR 2

/* sh.cond */
#define CSKIP    (1<<0)
#define CHADIF   (1<<1)
#define CHADELSE (1<<2)
#define CHADTRUE (1<<3)

#define CSHIFT 4
#define CGUARD (CHADIF << 7*CSHIFT)

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

	int state;       /* of parser */
	int count;       /* of Arg-s laid out so far */
	char** envp;

	char* heap;
	char* esep;
	char* csep;
	char* hptr;
	char* hend;

	char* var;

	int cond;
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

char* valueof(struct sh* ctx, char* var);
void define(struct sh* ctx, char* var, char* val);
void undef(struct sh* ctx, char* var);

void statement(struct sh* ctx, int argc, char** argv);

#define NR __attribute__((noreturn))
void fail(const char* err, char* arg, long ret) NR;
int error(struct sh* ctx, const char* err, char* arg, long ret);
void fatal(struct sh* ctx, const char* err, char* arg) NR;
int fchk(long ret, struct sh* ctx, const char* msg, char* arg);
int numargs(struct sh* ctx, int argc, int min, int max);
int argint(struct sh* ctx, char* arg, int* dst);
int argu64(struct sh* ctx, char* arg, uint64_t* dst);

int mmapfile(struct mbuf* mb, char* name);
int munmapfile(struct mbuf* mb);
