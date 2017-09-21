/* mbuf mapping modes */

#define NEWMAP 0
#define STRICT 1
#define FAILOK 2

struct mbuf {
	char* buf;
	long len;
	long full;
	int tried;
};

struct line {
	char* ptr;
	char* sep;
	char* end;
};

struct top {
	int argc;
	int argi;
	int opts;

	char** argv;
	char** envp;

	void* brk;
	void* lwm;
	void* ptr;
	void* end;

	struct mbuf modules_dep;
	struct mbuf modules_alias;
	struct mbuf config;

	char** deps;

	char* release;

	int nmatching;
	int ninserted;
};

typedef char* (*lnmatch)(char* ls, char* le, char* tag, int len);

#define CTX struct top* ctx

void insmod(CTX, char* name, char* opts);
void prep_release(CTX);
void prep_modules_dep(CTX);
char** query_deps(CTX, char* name);
char* query_pars(CTX, char* name);
char* query_alias(CTX, char* name);
int is_blacklisted(CTX, char* name);

void mmap_whole(struct mbuf* mb, char* name, int mode);
void decompress(CTX, struct mbuf* mb, char* path, char* cmd);

void* heap_alloc(CTX, int size);
void unmap_buf(struct mbuf* mb);
void flush_heap(CTX);

char* heap_dup(CTX, char* str);
char* heap_dupe(CTX, char* p, char* e);
