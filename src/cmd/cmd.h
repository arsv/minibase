#include <bits/ioctl/tty.h>
#include <cdefs.h>

struct heap {
	void* brk;
	void* ptr;
	void* end;
};

struct outbuf {
	char* buf;
	int ptr;
	int len;
};

struct fname;

struct tabtab {
	void* buf;     /* mmaped; may get remapped in process */
	int size;      /* ... of the buffer */
	int ptr;       /* ... to the start of free space in the buffer */
        /* the buffer contains a bunch of struct fname-s followed by index */
	struct fname** idx; /* file name index */
	int count;     /* Number of (int) entries there. */
	int needexe;   /* Ignore non-executable files when Tab'ing a command */
};

struct history {
	char* buf;
	int size;

	int len;
	int cur;

	int temp;
};

struct environ {
	void* buf;
	int size;

	int sep;
	int ptr;

	int count;
	char** orig;
};

struct top {
	int argc;
	char** argv;
	char** envp;

	struct heap heap;
	struct outbuf out;
	struct tabtab tts;
	struct history hst;
	struct environ env;

	int sigfd;
	int cols;
	struct termios tso;
	struct termios tsi;

	char* buf;    /* ~/foo/bar > cat something.tx...........  */
	int sep;      /*         sep^             ptr^       max^ */
	int cur;
	int ptr;
	int max;

	int show;
	int ends;
	int viswi;
	int redraw;

	int esc;
	int tab;
};

#define CTX struct top* ctx __unused

void exit(CTX, int code) noreturn;
void quit(CTX, const char* msg, char* arg, int err) noreturn;

void init_input(CTX);
void fini_input(CTX);
void update_winsz(CTX);
int handle_input(CTX, char* buf, int len);
void cancel_input(CTX);

void prep_prompt(CTX);
void insert(CTX, char* inp, int len);
void replace(CTX, char* buf, int len);

void parse(CTX, char* buf, int len);
void execute(CTX, int argc, char** argv);

int extend(CTX, int len);
void* alloc(CTX, int len);

void single_tab(CTX);
void double_tab(CTX);
void cancel_tab(CTX);
void list_cwd(CTX);

void hist_prev(CTX);
void hist_next(CTX);
void hist_store(CTX);

void envp_dump(CTX, char* name);
void envp_set(CTX, char* name, char* value);
void envp_unset(CTX, char* name);
void envp_dump_all(CTX);

/* This structure is only used in _tabtab but hardly warrants its own header */

struct exparg {
	char* buf;
	int ptr;
	int end;

	char* dir;
	char* base;
	int blen;

	int noslash;
	int initial;
	char quote;
};

int expand_arg(CTX, struct exparg* xa);
void free_exparg(struct exparg* xa);

#define TT struct tabtab* tt
#define XA struct exparg* xa
