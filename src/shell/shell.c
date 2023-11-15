#include <sys/file.h>
#include <sys/signal.h>
#include <sys/ppoll.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <output.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

#include "shell.h"

#define OPTS "d"
#define OPT_d (1<<0)

#define S_SEP 0
#define S_ARG 1
#define S_QUOT 2
#define S_QEND 3
#define S_PASS 4
#define S_EXEC 5

ERRTAG("shell");

char inbuf[1024];
char argbuf[2048];

struct shell sh;

/* Command line parsing */

static void reset_args(void)
{
	sh.argsep = 0;
	sh.argptr = 0;
	sh.argidx = 0;
	sh.argcnt = 0;
}

static int syntax(char c, char* msg)
{
	warn(msg, NULL, 0);

	reset_args();

	if(c == '\n')
		return S_SEP;
	else
		return S_PASS;
}

static int add_char(char c, int s)
{
	int ptr = sh.argptr;
	int max = sizeof(argbuf);

	if(ptr >= max)
		return syntax(c, "overflow");

	argbuf[ptr++] = c;

	sh.argptr = ptr;

	return s;
}

static int end_arg(char c, int s)
{
	int ptr = sh.argptr;
	int maxptr = sizeof(argbuf);

	if(ptr >= maxptr)
		return syntax(c, "overflow");

	argbuf[ptr++] = '\0';
	sh.argptr = ptr;

	int cnt = sh.argcnt;
	int sep = sh.argsep;
	int maxcnt = ARRAY_SIZE(sh.args);

	if(cnt >= maxcnt)
		return syntax(c, "overargs");

	sh.args[cnt] = &argbuf[sep];
	sh.argsep = ptr;
	sh.argcnt = cnt + 1;

	return s;
}

static int disp_pass(char c)
{
	if(c == '\n')
		return S_SEP;

	return S_PASS;
}

static int disp_qend(char c)
{
	if(c == ' ' || c == '\t' || !c)
		return end_arg(c, S_SEP);
	if(c == '\n')
		return end_arg(c, S_EXEC);

	return syntax(c, "garbage after closing quote");
}

static int disp_quot(char c)
{
	if(c == '"')
		return S_QEND;
	if(c == '\n')
		return syntax(c, "newline in quotes");

	return add_char(c, S_QUOT);
}

static int disp_arg(char c)
{
	if(c == ' ' || c == '\t' || !c)
		return end_arg(c, S_SEP);
	if(c == '\n')
		return end_arg(c, S_EXEC);

	return add_char(c, S_ARG);
}

static int disp_sep(char c)
{
	if(c == ' ' || c == '\t' || !c)
		return S_SEP;
	if(c == '\n')
		return S_EXEC;
	if(c == '"')
		return S_QUOT;

	return add_char(c, S_ARG);
}

static int dispatch(int s, char c)
{
	switch(s) {
		case S_SEP:
			return disp_sep(c);
		case S_ARG:
			return disp_arg(c);
		case S_QUOT:
			return disp_quot(c);
		case S_QEND:
			return disp_qend(c);
		case S_PASS:
			return disp_pass(c);
		default:
			fail("unexpected parser state", NULL, s);
	}
}

static int complete(int s)
{
	run_command();
	//bufoutflush(&bo);
	reset_heap();
	reset_args();

	return S_SEP;
}

static void parse_input(char* buf, int len)
{
	int s = sh.state;

	for(int i = 0; i < len; i++) {
		s = dispatch(s, buf[i]);

		if(s != S_EXEC) continue;

		s = complete(s);
	}

	sh.state = s;
}

static void input_loop(void)
{
	char* buf = inbuf;
	int rd, len = sizeof(inbuf);

	if(!(rd = sys_read(STDIN, buf, len))) {
		_exit(0x00);
	} else if(rd < 0) {
		if(rd == -EINTR)
			return;
		fail(NULL, "stdin", rd);
	} else {
		parse_input(inbuf, rd);
	}
}

int main(int argc, char** argv)
{
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		sh.opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	sh.envp = argv + argc + 1;

	init_heap();

	warn("ready to accept commands", NULL, 0);

	while(1) input_loop();
}
