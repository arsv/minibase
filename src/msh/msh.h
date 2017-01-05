struct sh {
	char* file;
	int line;

	char* heap;
	char* hend;
	char* hptr;

	int state;
	char* var;
	int count;

	char** envp;

	int ret;
};

void hinit(struct sh* ctx);
void* halloc(struct sh* ctx, int len);
void hrevert(struct sh* ctx, void* ptr);

void parse(struct sh* ctx, char* buf, int len);
void pfini(struct sh* ctx);

char* valueof(struct sh* ctx, char* var);

void exec(struct sh* ctx, int argc, char** argv);

#define NR __attribute__((noreturn))
void fail(const char* err, char* arg, long ret) NR;
int error(struct sh* ctx, const char* err, char* arg, long ret);
void fatal(struct sh* ctx, const char* err, char* arg) NR;
