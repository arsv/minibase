#include <sys/inotify.h>
#include <sys/read.h>
#include <sys/alarm.h>
#include <sys/stat.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

ERRTAG = "waitfor";
ERRLIST = {
	REPORT(EBADF), REPORT(EFAULT), REPORT(ENOTDIR), REPORT(EINTR),
	REPORT(EINVAL), REPORT(ENOMEM), REPORT(ENFILE), REPORT(EMFILE),
	REPORT(EACCES), REPORT(EBADF), REPORT(ENOENT), REPORT(ELOOP),
	RESTASNUMBERS
};

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

	long ret = sysstat(path, &st);

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

	while((rd = sysread(fd, inobuf, inolen)) > 0) {
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
	long wd;
	int proper = 0;

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

	xchk(sys_inotify_rm_watch(fd, wd),
	     "inotify-rm-watch", name);
}

int main(int argc, char** argv)
{
	int i = 1;
	int timeout = TIMEOUT;

	if(i < argc && argv[i][0] == '+')
		timeout = intval(argv[i++] + 1);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	sysalarm(timeout);

	int fd = xchk(sys_inotify_init(), "inotify-init", NULL);

	for(; i < argc; i++)
		waitfor(fd, argv[i]);

	return 0;
}
