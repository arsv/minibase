#include <sys/file.h>
#include <sys/sched.h>
#include <sys/inotify.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("waitfor");
ERRLIST(NEBADF NEFAULT NENOTDIR NEINTR NEINVAL NENOMEM NENFILE NEMFILE
	NEACCES NEBADF NENOENT NELOOP);

#define PAGE 1024
#define TIMEOUT 5

char inobuf[PAGE];

static int intval(char* arg)
{
	int n;
	char* p = parseint(arg, &n);

	if(!p || *p)
		fail("not a number:", arg, 0);

	return n;
}

static int check_file(const char* path)
{
	struct stat st;

	long ret = sys_stat(path, &st);

	if(ret >= 0)
		return 1;
	if(ret == -ENOENT)
		return 0;
	else
		fail(NULL, path, ret);
}

static int match_event(struct inotify_event* ino, char* name)
{
	return !strcmp(name, ino->name);
}

static void watch_ino_for(int fd, char* name)
{
	int inolen = sizeof(inobuf);
	long rd;

	while((rd = sys_read(fd, inobuf, inolen)) > 0) {
		char* inoend = inobuf + rd;
		char* p = inobuf;

		while(p < inoend) {
			struct inotify_event* ino = (void*) p;

			if(match_event(ino, name))
				return;

			p += sizeof(*ino) + ino->len;
		}

	} if(rd < 0) {
		fail("read", NULL, rd);
	}
}

static void waitfor(int fd, char* name)
{
	int nlen = strlen(name);
	char* sep = strerev(name, name + nlen, '/');
	char dir[sep - name + 3];
	int wd, ret, proper = 0;

	if(sep > name + 1) {
		int len = sep - name - 1;
		memcpy(dir, name, len);
		dir[len] = '\0';
		proper = 1;
	} else if(sep > name) {
		dir[0] = '/';
		dir[1] = '\0';
	} else {
		dir[0] = '.';
		dir[1] = '\0';	
	}

	wd = sys_inotify_add_watch(fd, dir, IN_CREATE);

	if(wd == -ENOENT && proper) {
		if(strlen(dir) < strlen(name) && strcmp(dir, "."))
			waitfor(fd, dir);
		wd = sys_inotify_add_watch(fd, dir, IN_CREATE);
	}
	
	if(wd < 0)
		fail("inotify-add-watch", name, wd);

	if(check_file(name))
		;
	else
		watch_ino_for(fd, sep);

	if((ret = sys_inotify_rm_watch(fd, wd)) < 0)
		fail("inotify-rm-watch", name, ret);
}

int main(int argc, char** argv)
{
	int i = 1, timeout = TIMEOUT, fd;

	if(i < argc && argv[i][0] == '+')
		timeout = intval(argv[i++] + 1);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	sys_alarm(timeout);

	if((fd = sys_inotify_init()) < 0)
		fail("inotify-init", NULL, fd);

	for(; i < argc; i++)
		waitfor(fd, argv[i]);

	return 0;
}
