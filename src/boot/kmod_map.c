#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mmap.h>
#include <sys/pipe.h>
#include <sys/fcntl.h>
#include <sys/fork.h>
#include <sys/exec.h>
#include <sys/wait.h>

#include <exit.h>
#include <fail.h>
#include <util.h>

#include "kmod.h"

/* The module must be loaded to process memory prior to init_module call.
   Uncompressed modules are mmaped whole. For compresses ones, the output
   of decompressor process is read into a growing chunk of memory. */

#define PAGE 4096
#define MAX_FILE_SIZE 20*1024*1024 /* 20MB */

static char* mmapanon(long size)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	long ret = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(ret))
		fail("mmap", NULL, ret);

	return (char*)ret;
}

static char* mextend(char* buf, long size, long newsize)
{
	long ret = sys_mremap(buf, size, newsize, MREMAP_MAYMOVE);

	if(mmap_error(ret))
		fail("mremap", NULL, ret);

	return (char*)ret;
}

static int child(int fds[2], char* cmd, char* path, char** envp)
{
	char* argv[] = { cmd, path, NULL };

	xchk(sys_close(fds[0]), "close", NULL);
	xchk(sys_dup2(fds[1], STDOUT), "dup2", NULL);
	long ret = sys_execve(cmd, argv, NULL);

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

	while((rd = sys_read(fd, buf + ptr, len - ptr)) > 0) {
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
	xchk(sys_pipe2(fds, 0), "pipe", NULL);

	int pid = xchk(sys_fork(), "fork", NULL);

	if(pid == 0)
		_exit(child(fds, cmd, path, envp));

	sys_close(fds[1]);

	readall(mb, fds[0], cmd);

	int status;
	xchk(sys_waitpid(pid, &status, 0), "wait", cmd);

	if(status) fail("non-zero exit code in", cmd, 0);
}

void mmapwhole(struct mbuf* mb, char* name)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	ret = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if(mmap_error(ret))
		fail("cannot mmap", name, ret);

	if(st.size > MAX_FILE_SIZE)
		fail("file too large:", name, ret);

	mb->buf = (char*)ret;
	mb->len = mb->full = st.size;
}

void munmapbuf(struct mbuf* mb)
{
	sys_munmap(mb->buf, mb->full);
}
