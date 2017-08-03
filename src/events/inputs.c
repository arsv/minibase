#include <bits/ioctl/input.h>
#include <bits/errno.h>
#include <bits/major.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <output.h>
#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

#include "inputs.h"

ERRTAG = "inputs";
ERRLIST = {
	REPORT(EACCES), REPORT(EPERM), REPORT(ENOTTY), REPORT(ENODEV),
	REPORT(ENOTDIR), REPORT(EINVAL), RESTASNUMBERS
};

#define OPTS "ar"
#define OPT_a (1<<0)
#define OPT_r (1<<1)

static const char devinput[] = "/dev/input";

static void header(char* path, char* msg)
{
	int plen = strlen(path);
	int mlen = strlen(msg);

	char buf[plen + mlen + 4];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, path);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);
	*p++ = '\n';

	writeout(buf, p - buf);
}

static void shortinfo(char* path, int fd)
{
	char name[32+1];
	int nlen = 32;
	name[nlen] = '\0';

	if(sys_ioctl(fd, EVIOCGNAME(nlen), name) < 0)
		header(path, "(unnamed)");
	else if(!(nlen = strlen(name)))
		header(path, "(empty)");
	else
		header(path, name);
}

static void longinfo(char* path, int fd)
{
	shortinfo(path, fd);
	query_event_bits(fd);
}

static void with_input(char* path, dumper f)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(path, O_RDONLY)) < 0) {
		warn(NULL, path, fd);
		return;
	}

	if((ret = sys_fstat(fd, &st)) < 0) {
		warn("stat", path, ret);
		return;
	}

	if((st.mode & S_IFMT) != S_IFCHR)
		header(path, "not a char device");
	else if(major(st.rdev) != INPUT_MAJOR)
		header(path, "not an input device");
	else if(minor(st.rdev) < 64)
		header(path, "non-event input device");
	else
		f(path, fd);

	sys_close(fd);

	flushout();
}

void with_entry(const char* path, char* name, dumper f)
{
	int plen = strlen(path);
	int nlen = strlen(name);

	char buf[plen + nlen + 2];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, path);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	*p++ = '\0';

	return with_input(buf, f);
}

static void with_named(char* name, dumper f)
{
	if(*strcbrk(name, '/'))
		with_input(name, f);
	else
		with_entry(devinput, name, f);
}

static void forall_args(int i, int argc, char** argv, dumper f)
{
	for(; i < argc; i++)
		with_named(argv[i], f);
}

void event_monitor(int i, int argc, char** argv, int opts)
{
	if(i >= argc)
		fail("device name required", NULL, 0);
	if(i + 1 < argc)
		fail("too many arguments", NULL, 0);

	with_named(argv[i], read_events);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_r)
		event_monitor(i, argc, argv, opts);
	else if(i < argc)
		forall_args(i, argc, argv, longinfo);
	else if(opts & OPT_a)
		forall_inputs(devinput, longinfo);
	else
		forall_inputs(devinput, shortinfo);

	return 0;
}
