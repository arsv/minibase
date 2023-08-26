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

int opts;
char** envp;

char inbuf[256];

int parser_state;

char argbuf[512];

int argsep;
int argptr;

int argidx;
int argcount;

char* args[20];

char tmpbuf[1024];
char outbuf[2048];
struct bufout bo;

char* shift(void)
{
	if(argidx >= argcount)
		return NULL;

	return args[argidx++];
}

void output(char* str, int len)
{
	int ret;

	if((ret = bufout(&bo, str, len)) < 0)
		fail("write", NULL, 0);
}

void outstr(char* str)
{
	output(str, strlen(str));
}

/* Command line parsing */

static int reset_args(void)
{
	argsep = 0;
	argptr = 0;
	argidx = 0;
	argcount = 0;
}

static int valid_end(int s)
{
	return (s == S_SEP);
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
	if(argptr >= sizeof(argbuf))
		return syntax(c, "overflow");

	argbuf[argptr++] = c;

	return s;
}

static int end_arg(char c, int s)
{
	if(argptr >= sizeof(argbuf))
		return syntax(c, "overflow");

	argbuf[argptr++] = '\0';

	if(argcount >= ARRAY_SIZE(args))
		return syntax(c, "overargs");

	args[argcount++] = &argbuf[argsep];
	argsep = argptr;

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
	if(s != S_EXEC)
		return s;

	run_command();
	bufoutflush(&bo);
	reset_args();

	return S_SEP;
}

static void parse_input(char* buf, int len)
{
	int s = parser_state;

	for(int i = 0; i < len; i++) {
		s = dispatch(s, buf[i]);
		s = complete(s);
	}

	parser_state = s;
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
	int ret;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	envp = argv + argc + 1;

	bufoutset(&bo, STDOUT, outbuf, sizeof(outbuf));

	warn("ready to accept commands", NULL, 0);

	while(1) input_loop();
}
