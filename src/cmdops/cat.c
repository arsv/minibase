#include <sys/file.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <errtag.h>
#include <util.h>

ERRTAG("cat");
ERRLIST(NENOENT NEAGAIN NEBADF NEFAULT NEINTR NEINVAL NEIO NEISDIR NEDQUOT
        NENOSPC NEDQUOT NENOTDIR NEACCES NEPERM NEFBIG NENOSPC NEPERM NEPIPE);

/* In addition to what the tradition cat does, this version also
   handles writing to files. In POSIX it would be done with output
   redirection, "cat > file", but minibase opts for "cat -o file".

   There's a certain overlap between cat, msh write builtin, cpy
   and bcp, all of which send data between files. In particulear,
   the following two do almost the same thing:

       cat -o output input
       cpy -o output input

   The difference is minute but may be important: cat overwrites
   the *contents* of output, leaving the inode data intact, while
   cpy replaces the *inode* with a new one. */

#define OPTS "oae"
#define OPT_o (1<<0)
#define OPT_a (1<<1)
#define OPT_e (1<<2)

struct top {
	int ofd;
	int ifd;
	char* oname;
	char* iname;

	void* brk;
	long len;
};

#define CATBUF 1024*1024      /* r/w block 1MB    */
#define SENDSIZE 0x7ffff000   /* from sendfile(2) */

static void prep_rwbuf(struct top* ctx)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + CATBUF);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->brk = brk;
	ctx->len = end - brk;
}

/* sendfile is tricky and may refuse transfer if it does not like
   either input or output fds. If so, it fails immediately on the
   first call with EINVAL.

   Because of sendfile attempt, cat wastes something like 3 syscalls
   whenever it fails. Probably no big deal. */

static void readwrite(struct top* ctx)
{
	int ifd = ctx->ifd;
	int ofd = ctx->ofd;

	long rd, wr;

	if(!ctx->brk)
		prep_rwbuf(ctx);

	void* buf = ctx->brk;
	long len = ctx->len;

	while((rd = sys_read(ifd, buf, len)) > 0)
		if((wr = writeall(ofd, buf, rd)) < 0)
			fail("write", ctx->oname, wr);
	if(rd < 0)
		fail("read", ctx->iname, rd);
}

static int sendfile(struct top* ctx)
{
	int ifd = ctx->ifd;
	int ofd = ctx->ofd;

	long ret;
	long len = SENDSIZE;
	int first = 1;

	while((ret = sys_sendfile(ofd, ifd, NULL, len)) > 0)
		first = 0;

	if(ret >= 0)
		return ret;
	if(first && ret == -EINVAL)
		return ret;
	
	fail("sendfile", ctx->iname, ret);
}

static void cat(struct top* ctx)
{
	if(sendfile(ctx) >= 0)
		return;
	else
		readwrite(ctx);
}

static void open_output(struct top* ctx, char* name, int opts)
{
	int fd;
	int flags = O_WRONLY;

	if(opts & OPT_a)
		flags |= O_APPEND;
	else
		flags |= O_TRUNC | O_CREAT;

	if((fd = sys_open3(name, flags, 0666)) < 0)
		fail(NULL, name, fd);

	ctx->ofd = fd;
	ctx->oname = name;
}

static void open_input(struct top* ctx, char* name)
{
	int fd;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ctx->ifd = fd;
	ctx->iname = name;
}

int main(int argc, char** argv)
{
	int i = 1, opts = 0;

	struct top ctx = {
		.ofd = STDOUT,
		.oname = "<stdout>",
		.ifd = STDIN,
		.iname = "<stdin>",
		.brk = NULL,
		.len = 0
	};

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if((opts & OPT_e) && (opts & (OPT_o | OPT_a)))
		fail("cannot mix -e and -ao", NULL, 0);

	if(opts & (OPT_o | OPT_a)) {
		if(i >= argc)
			fail("argument required", NULL, 0);
		open_output(&ctx, argv[i++], opts);
	} else if(opts & OPT_e) {
		ctx.ofd = STDERR;
	}

	if(i >= argc) {
		cat(&ctx);
	} else while(i < argc) {
		open_input(&ctx, argv[i++]);
		cat(&ctx);
	}

	return 0;
}
