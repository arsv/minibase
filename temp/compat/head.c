#include <sys/file.h>

#include <string.h>
#include <util.h>
#include <main.h>

ERRTAG("head");

/* Superseeded by tail -h */

static char buf[2*4096];

static void output(char* buf, int len)
{
	int wr;

	if((wr = writeall(STDOUT, buf, len)) < 0)
		fail("write", "STDOUT", wr);
}

static void run_pipe(int count, int fd, char* name)
{
	int len = sizeof(buf);
	int rd, seen = 0;

	while((rd = sys_read(fd, buf, len))) {
		char* p = buf;

		while(p < buf + rd)
			if(*p++ != '\n')
				continue;
			else if(++seen >= count)
				break;

		if(p > buf)
			output(buf, p - buf);
		if(p < buf + rd)
			break;

	} if(rd < 0) fail(NULL, name, rd);
}

static void run_file(int count, char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	run_pipe(count, fd, name);
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static void parse_count(char* arg, int* count)
{
	int cnt = 0;

	for(; isdigit(*arg); arg++)
		cnt = 10*cnt + (*arg - '0');
	if(cnt)
		*count = cnt;

	if(*arg)
		fail("unexpected options:", arg, 0);
}

int main(int argc, char** argv)
{
	int i = 1;
	int count = 10;

	if(i < argc && argv[i][0] == '-')
		parse_count(argv[i++] + 1, &count);

	if(i < argc - 1)
		fail("too many arguments", NULL, 0);

	if(i < argc)
		run_file(count, argv[i]);
	else
		run_pipe(count, STDIN, NULL);

	return 0;
}
