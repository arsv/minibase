#include <bits/ioctl/tty.h>

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

struct tabtab {
	char* buf;
	int size;
	int sep;
	int ptr;
};

struct top {
	int argc;
	char** argv;
	char** envp;

	struct heap heap;
	struct outbuf out;
	struct tabtab tts;

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

void prep_prompt(CTX);
void insert(CTX, char* inp, int len);

void parse(CTX, char* buf, int len);
void execute(CTX, int argc, char** argv);

int extend(CTX, int len);
void* alloc(CTX, int len);

void single_tab(CTX);
void double_tab(CTX);
void cancel_tab(CTX);
