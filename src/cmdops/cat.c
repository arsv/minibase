#include <sys/file.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <errtag.h>
#include <util.h>

ERRTAG("cat");
ERRLIST(NEAGAIN NEBADF NEFAULT NEINTR NEINVAL NEIO NEISDIR NEDQUOT
	NEFBIG NENOSPC NEPERM NEPIPE);

/* cat is just too simple to tolerate as is, so here's a twist:
   this version uses sendfile(2) if possible, falling back to
   read/write cycle if that fails.

   sendfile is tricky and may refuse transfer if it does not like
   either input or output fds. If so, it fails immediately on the
   first call with EINVAL.

   Because of sendfile attempt, cat wastes something like 3 syscalls
   whenever it fails. Probably no big deal. */

struct cbuf {
	void* brk;
	long len;
};

#define CATBUF 1024*1024      /* r/w block 1MB    */
#define SENDSIZE 0x7ffff000   /* from sendfile(2) */

static long dump(char* buf, long len)
{
	long wr;

	while(len > 0) {
		if((wr = sys_write(STDOUT, buf, len)) < 0)
			return wr;

		buf += wr;
		len -= wr;
	}

	return 0;
}

static void prepbuf(struct cbuf* cb)
{
	if(cb->brk)
		return;

	void* brk = sys_brk(0);
	void* end = sys_brk(brk + CATBUF);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	cb->brk = brk;
	cb->len = end - brk;
}

static void readwrite(struct cbuf* cb, char* name, int fd)
{
	long rd, wr;

	prepbuf(cb);

	void* buf = cb->brk;
	long len = cb->len;

	while((rd = sys_read(fd, buf, len)) > 0)
		if((wr = dump(buf, rd)) < 0)
			fail("write", "stdout", wr);
	if(rd < 0)
		fail("read", name, rd);
}

static int sendfile(char* name, int fd)
{
	long ret;
	long len = SENDSIZE;
	int first = 1;

	while((ret = sys_sendfile(STDOUT, fd, NULL, len)) > 0)
		first = 0;

	if(ret >= 0)
		return ret;
	if(first && ret == -EINVAL)
		return ret;
	
	fail("sendfile", name, ret);
}

static void cat(struct cbuf* cb, char* name, int fd)
{
	if(sendfile(name, fd) >= 0)
		return;
	else
		readwrite(cb, name, fd);
}

static int xopen(const char* name)
{
	return xchk(sys_open(name, O_RDONLY), NULL, name);
}

int main(int argc, char** argv)
{
	int i;
	struct cbuf cb = { NULL, 0 };

	if(argc < 2)
		cat(&cb, "stdin", 0);
	else for(i = 1; i < argc; i++)
		cat(&cb, argv[i], xopen(argv[i]));

	return 0;
}
