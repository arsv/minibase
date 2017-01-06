/* arguments for hset/hreset */
#define HEAP 0
#define ESEP 1
#define CSEP 2
#define VSEP 3

/* env entry types */
#define ENVDEL 0
#define ENVSTR 1
#define ENVPTR 2

struct sh {
	char* file;
	int line;

	int state;
	int count;
	char** envp;
	int ret;

	char* heap;
	char* esep;
	char* csep;
	char* hptr;
	char* hend;

	char* var;
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

void hinit(struct sh* ctx);
void* halloc(struct sh* ctx, int len);
void hrev(struct sh* ctx, int type);
void hset(struct sh* ctx, int what);

void parse(struct sh* ctx, char* buf, int len);
void pfini(struct sh* ctx);

char* valueof(struct sh* ctx, char* var);
void define(struct sh* ctx, char* var, char* val);
void undef(struct sh* ctx, char* var);

void exec(struct sh* ctx, int argc, char** argv);

#define NR __attribute__((noreturn))
void fail(const char* err, char* arg, long ret) NR;
int error(struct sh* ctx, const char* err, char* arg, long ret);
void fatal(struct sh* ctx, const char* err, char* arg) NR;
