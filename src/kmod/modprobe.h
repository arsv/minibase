/* mbuf mapping modes */

#define SKIP 0
#define WARN 1
#define FAIL 2

struct line {
	char* ptr;
	char* sep;
	char* val;
	char* end;
};

struct top {
	int argc;
	int argi;
	int opts;

	int nofail;

	char** argv;
	char** envp;

	char* base;

	void* brk;
	void* lwm;
	void* ptr;
	void* end;

	struct mbuf modules_dep;
	struct mbuf modules_alias;
	struct mbuf config;

	int tried_modules_dep;
	int tried_modules_alias;
	int tried_config;

	char** deps;

	char* release;

	int nmatching;
	int ninserted;
};

#define CTX struct top* ctx __unused

//void insmod(CTX, char* name, char* opts);
//int query_deps(CTX, struct line* ln, char* name);
//int query_pars(CTX, struct line* ln, char* name);
//int query_alias(CTX, struct line* ln, char* name);
//int blacklisted(CTX, char* name);
//
//void* heap_alloc(CTX, int size);
//void unmap_buf(struct mbuf* mb);
//void flush_heap(CTX);
//
//char* heap_dup(CTX, char* str);
//char* heap_dupe(CTX, char* p, char* e);
//
//int error(CTX, const char* msg, char* arg, int err);
