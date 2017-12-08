#include <bits/ioctl/tty.h>

struct top {
	int argc;
	char** argv;
	char** envp;

	void* hbrk;
	void* hptr;
	void* hend;

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

	char* outbuf;
	int outptr;
	int outlen;
};

#define CTX struct top* ctx __unused

void exit(CTX, int code) noreturn;
void quit(CTX, const char* msg, char* arg, int err) noreturn;

void init_input(CTX);
void fini_input(CTX);
void update_winsz(CTX);
int handle_input(CTX, char* buf, int len);

void prep_prompt(CTX);

void parse(CTX, char* buf, int len);

void execute(CTX, int argc, char** argv);

int extend(CTX, int len);
void* alloc(CTX, int len);
