/* mbuf mapping modes */

#define SKIP 0
#define WARN 1
#define FAIL 2

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
	struct mbuf modules_builtin;
	struct mbuf config;

	char** deps;

	char* release;

	int nmatching;
	int ninserted;
};

#define CTX struct top* ctx __unused
