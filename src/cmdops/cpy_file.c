#include <sys/dents.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "cpy.h"

/* Contents trasfer for regular files. Surprisingly non-trivial task
   in Linux if we want to keep things fast and reliable. */

#define RWBUFSIZE 1024*1024

static int sendfile(CCT, DST, SRC, uint64_t* size)
{
	uint64_t done = 0;
	uint64_t need = *size;
	long ret = 0;
	long run = 0x7ffff000;

	int outfd = dst->fd;
	int infd = src->fd;

	if(need < run)
		run = need;

	while(done < need) {
		if((ret = sys_sendfile(outfd, infd, NULL, run)) <= 0)
			break;
		done += ret;
	};

	if(ret >= 0)
		return 0;
	if(!done && ret == -EINVAL)
		return -1;

	failat("sendfile", dst->dir, dst->name, ret);
}

static void alloc_rw_buf(CTX)
{
	if(ctx->buf)
		return;

	char* buf = sys_brk(0);
	char* end = sys_brk(buf + RWBUFSIZE);

	if(brk_error(buf, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->buf = buf;
	ctx->len = end - buf;
}

static void readwrite(CCT, DST, SRC, uint64_t* size)
{
	struct top* ctx = cct->top;

	alloc_rw_buf(ctx);

	uint64_t need = *size;
	uint64_t done = 0;

	char* buf = ctx->buf;
	long len = ctx->len;

	if(len > need)
		len = need;

	int rd = 0, wr;
	int rfd = src->fd;
	int wfd = dst->fd;

	while(done < need) {
		if((rd = sys_read(rfd, buf, len)) <= 0)
			break;
		if((wr = writeall(wfd, buf, rd)) < 0)
			failat("write", dst->dir, dst->name, wr);
		done += rd;
	} if(rd < 0) {
		failat("read", src->dir, src->name, rd);
	}
}

/* Sendfile may not work on a given pair of descriptors for
   various reasons. If this happens, we fall back to read/write
   calls. Generally the reasons depend on directory (and the underlying
   fs) so if sendfile fails for one file we stop using it for the whole
   directory. */

void transfer(CCT, DST, SRC, uint64_t* size)
{
	if(!cct->nosf)
		goto rw;

	if(sendfile(cct, dst, src, size) >= 0)
		return;
rw:
	cct->nosf = 1;

	readwrite(cct, dst, src, size);
}
