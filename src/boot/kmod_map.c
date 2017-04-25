#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <sys/mremap.h>
#include <sys/munmap.h>
#include <sys/read.h>
#include <sys/pipe2.h>
#include <sys/dup2.h>
#include <sys/close.h>
#include <sys/fork.h>
#include <sys/execve.h>
#include <sys/waitpid.h>
#include <sys/_exit.h>

#include <fail.h>
#include <util.h>

#include "kmod.h"

#define PAGE 4096

#define MAX_FILE_SIZE 20*1024*1024 /* 20MB */

static char* mmapanon(long size)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	long ret = sysmmap(NULL, size, prot, flags, -1, 0);

	if(MMAPERROR(ret))
		fail("mmap", NULL, ret);

	return (char*)ret;
}

static char* mextend(char* buf, long size, long newsize)
{
	long ret = sysmremap(buf, size, newsize, MREMAP_MAYMOVE);

	if(MMAPERROR(ret))
		fail("mremap", NULL, ret);

	return (char*)ret;
}

static int child(int fds[2], char* cmd, char* path, char** envp)
{
	char* argv[] = { cmd, path, NULL };

	xchk(sysclose(fds[0]), "close", NULL);
	xchk(sysdup2(fds[1], STDOUT), "dup2", NULL);
	long ret = sysexecve(cmd, argv, NULL);

	fail("cannot exec", cmd, ret);
	return 0xFF;
}

void readall(struct mbuf* mb, int fd, char* cmd)
{
	const int unit = 4*PAGE;
	long len = unit;
	long ptr = 0;
	long rd;

	char* buf = mmapanon(unit);

	while((rd = sysread(fd, buf + ptr, len - ptr)) > 0) {
		ptr += rd;
		if(ptr < len) continue;
		buf = mextend(buf, len, len + unit);
		len += unit;
	} if(rd < 0) {
		fail("read", cmd, rd);
	}

	mb->buf = buf;
	mb->len = ptr;
	mb->full = len;
}

void decompress(struct mbuf* mb, char* path, char* cmd, char** envp)
{
	int fds[2];
	xchk(syspipe2(fds, 0), "pipe", NULL);

	int pid = xchk(sysfork(), "fork", NULL);

	if(pid == 0)
		_exit(child(fds, cmd, path, envp));

	sysclose(fds[1]);

	readall(mb, fds[0], cmd);

	int status;
	xchk(syswaitpid(pid, &status, 0), "wait", cmd);

	if(status) fail("non-zero exit code in", cmd, 0);
}

void mmapwhole(struct mbuf* mb, char* name)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sysopen(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sysfstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	ret = sysmmap(NULL, st.st_size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		fail("cannot mmap", name, ret);

	if(st.st_size > MAX_FILE_SIZE)
		fail("file too large:", name, ret);

	mb->buf = (char*)ret;
	mb->len = mb->full = st.st_size;
}

void munmapbuf(struct mbuf* mb)
{
	sysmunmap(mb->buf, mb->full);
}
