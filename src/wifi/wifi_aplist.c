#include <sys/mman.h>

#include <nlusctl.h>
#include <util.h>

#include "wifi.h"
#include "common.h"

void* heap_alloc(CTX, int size)
{
	void *brk, *ptr, *end;

	if(!(brk = ctx->brk)) {
		brk = sys_brk(NULL);
		ptr = brk;
		end = brk;
		ctx->brk = brk;
		ctx->end = end;
	} else {
		ptr = ctx->ptr;
		end = ctx->end;
	}

	void* newptr = ptr + size;

	if(newptr > end) {
		long need = pagealign(newptr - end);
		void* newend = sys_brk(end + need);

		if(newptr > newend)
			fail("brk", NULL, -ENOMEM);

		ctx->end = newend;
	}

	ctx->ptr = newptr;

	return ptr;
}

static void heap_trim(CTX, void* to)
{
	void* brk = ctx->brk;
	void* ptr = ctx->ptr;

	if(to < brk || to > ptr)
		fail("invalid trim", NULL, 0);

	ctx->ptr = to;
}

static void index_entries(CTX, int count, void* brk, void* end)
{
	struct ucattr** idx = heap_alloc(ctx, count*sizeof(void*));
	int i = 0;
	void* ptr = brk;
	
	while(ptr < end) {
		struct ucattr* msg = ptr;

		if(i >= count || msg->len < sizeof(*msg))
			fail("indexing failure", NULL, 0);

		idx[i++] = msg;
		ptr += msg->len;
	}

	ctx->scans = idx;
	ctx->count = count;
}

void fetch_scan_list(CTX)
{
	char cmdbuf[128];
	struct ucbuf uc;
	struct ucattr* msg;
	int start = 0;
	int ret, fd = ctx->fd;
	int rep, count = 0;
	int* next;

	int replen = PAGE;
	void* repbuf = heap_alloc(ctx, replen);
	void* repbrk = repbuf;
	void* repend = repbuf;
again:
	uc_buf_set(&uc, cmdbuf, sizeof(cmdbuf));
	uc_put_hdr(&uc, CMD_GETSCAN);
	uc_put_int(&uc, ATTR_NEXT, start);

	if((ret = uc_send(fd, &uc)) < 0)
		fail("send", NULL, ret);
	if((ret = uc_recv(fd, repbuf, replen)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(repbuf, ret)))
		fail("recv", NULL, -EBADMSG);

	if((rep = uc_repcode(msg)) == -ENOENT)
		goto done;
	else if(rep > 0)
		fail("unexpected notification", NULL, 0);
	else if(rep < 0)
		fail(NULL, NULL, rep);
	
	count++;
	repend = repbuf + msg->len;
	heap_trim(ctx, repend);

	if(!(next = uc_get_int(msg, ATTR_NEXT)))
		goto done;
	if(*next <= start)
		goto done;

	repbuf = heap_alloc(ctx, replen);
	start = *next;

	goto again;
done:
	index_entries(ctx, count, repbrk, repend);
}
