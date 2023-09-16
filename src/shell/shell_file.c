#include <sys/file.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "shell.h"

static int sum_arg_size(void)
{
	int i = sh.argidx;
	int n = sh.argcnt;
	int sum = 0;

	for(; i < n; i++)
		sum +=  strlen(sh.args[i]) + 1;

	return sum;
}

static int prepare_text(char* buf, int size)
{
	char* arg;

	char* p = buf;
	char* e = buf + size;

	while((arg = shift())) {
		p = fmtstr(p, e, arg);
		p = fmtchar(p, e, '\n');
	}

	return p - buf;
}

static void write_text(char* name, char* buf, int size)
{
	int fd, ret;
	int flags = O_WRONLY | O_TRUNC;

	if((fd = sys_open(name, flags)) < 0)
		return repl(NULL, NULL, fd);

	if((ret = sys_write(fd, buf, size)) < 0)
		return repl(NULL, NULL, ret);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

void cmd_write(void)
{
	char* name;
	int size;

	if(!(name = shift_arg()))
		return;

	if(!(size = sum_arg_size()))
		return repl("empty write", NULL, 0);

	char* buf = heap_alloc(size);

	size = prepare_text(buf, size);

	write_text(name, buf, size);
}
