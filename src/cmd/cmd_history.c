#include <sys/mman.h>
#include <printf.h>
#include <string.h>
#include "cmd.h"

#define MAXHISTORY 4*PAGE
#define TRIMCHUNK 1*PAGE /* should be less than MAXHISTORY */

#define HST struct history* hst

static int allocate_buffer(HST, int size)
{
	void* buf;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

	size += (PAGE - size % PAGE) % PAGE;

	buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(buf))
		return (long)buf;

	hst->buf = buf;
	hst->size = size;

	return 0;
}

static int extend_buffer(HST, int size)
{
	void* buf;
	int flags = MREMAP_MAYMOVE;

	size += (PAGE - size % PAGE) % PAGE;

	buf = sys_mremap(hst->buf, hst->size, size, flags);

	if(mmap_error(buf))
		return (long)buf;

	hst->buf = buf;
	hst->size = size;

	return 0;
}

static void trim_history(HST)
{
	char* buf = hst->buf;
	int ptr = TRIMCHUNK;
	int end = hst->len;

	while(ptr < end)
		if(buf[ptr] == '\n')
			break;

	if(ptr >= end) {
		hst->len = 0;
		hst->cur = 0;
		hst->temp = 0;
	} else {
		ptr++;
		memmove(buf, buf + ptr, end - ptr);
		hst->len -= ptr;

		if(hst->cur > ptr)
			hst->cur -= ptr;
		else
			hst->cur = 0;

		if(hst->temp > ptr)
			hst->temp -= ptr;
		else
			hst->temp = 0;
	}
}

static int prep_space(HST, int need)
{
	if(!hst->buf)
		return allocate_buffer(hst, need);
	if(hst->len + need > MAXHISTORY)
		trim_history(hst);
	if(hst->len + need <= hst->size)
		return 0;

	return extend_buffer(hst, hst->len + need);
}

static void store_line(CTX, int mark)
{
	struct history* hst = &ctx->hst;
	int len = ctx->ptr - ctx->sep;

	if(hst->temp)
		hst->len = hst->temp;
	if(len <= 0 || len > PAGE)
		return;
	if(prep_space(hst, len + 1))
		return;

	char* dst = hst->buf + hst->len;
	char* src = ctx->buf + ctx->sep;

	memcpy(dst, src, len);
	dst[len++] = '\n';

	hst->len += len;
	hst->cur = hst->len;
	hst->temp = mark;
}

static int skip_line_back(char* buf, int cur)
{
	if(cur > 0 && buf[cur-1] == '\n')
		cur--;
	while(cur > 0 && buf[cur-1] != '\n')
		cur--;

	return cur;
}

static int skip_line_next(char* buf, int cur, int max)
{
	while(cur < max && buf[cur] != '\n')
		cur++;
	if(cur < max)
		cur++;

	return cur;
}

static void set_input(CTX, HST, int cur, int end)
{
	char* buf = hst->buf;

	/* do not paste \n to the input buffer */
	if(end > cur)
		if(buf[end-1] == '\n')
			end--;
	if(end < cur) /* sanity check */
		return;

	replace(ctx, buf + cur, end - cur);
}

void hist_store(CTX)
{
	store_line(ctx, 0);
}

void hist_prev(CTX)
{
	struct history* hst = &ctx->hst;
	char* buf;

	if(!(buf = hst->buf))
		return;

	int old = hst->cur;
	int cur = skip_line_back(buf, old);

	if(cur >= old)
		return;
	if(hst->cur >= hst->len && ctx->ptr > ctx->sep)
		store_line(ctx, hst->len);

	set_input(ctx, hst, cur, old);

	hst->cur = cur;
}

void hist_next(CTX)
{
	struct history* hst = &ctx->hst;
	char* buf;

	if(!(buf = hst->buf))
		return;
	if(hst->cur >= hst->len)
		return;

	int max = hst->len;
	int old = hst->cur;
	int cur = skip_line_next(buf, old, max);
	int end = skip_line_next(buf, cur, max);

	if(cur < old)
		return;

	set_input(ctx, hst, cur, end);

	if(hst->temp && cur >= hst->temp) {
		hst->len = cur;
		hst->temp = 0;
	}

	hst->cur = cur;
}
