#include <sys/fpath.h>
#include <sys/mman.h>

#include <string.h>
#include <util.h>

#include "cmd.h"

int extend(CTX, int len)
{
	struct heap* hp = &ctx->heap;
	void* ptr = hp->ptr + len;

	if(ptr > hp->end) {
		long need = hp->end - ptr;

		need += (PAGE - need % PAGE) % PAGE;

		char* brk = hp->end;
		char* end = sys_brk(brk + need);

		if(brk_error(brk, end)) {
			warn("cannot allocate memory", NULL, 0);
			return -1;
		}

		hp->end = end;
	}

	hp->ptr = ptr;

	return 0;
}

void* alloc(CTX, int len)
{
	struct heap* hp = &ctx->heap;
	void* ret = hp->ptr;

	if(extend(ctx, len))
		return NULL;

	return ret;
}

static int maybe_cut_cwd(CTX, int ptr)
{
	char* home = getenv(ctx->envp, "HOME");
	int hlen = home ? strlen(home) : 0;
	char* cwd = ctx->buf;

	if(!hlen || ptr < hlen)
		return ptr;
	if(memcmp(cwd, home, hlen))
		return ptr;
	if(cwd[hlen] && cwd[hlen] != '/')
		return ptr;

	memmove(cwd + 1, cwd + hlen, ptr - hlen);
	cwd[0] = '~';
	ptr = ptr - hlen + 1;

	return ptr;
}

void prep_prompt(CTX)
{
	int ret;
	char* buf = ctx->buf;
	int max = ctx->max;
	int ptr = 0;

	char* sign = "> ";
	int slen = strlen(sign);

	if((ret = sys_getcwd(buf, max)) > 0)
		ptr = maybe_cut_cwd(ctx, ret);

	if(ptr + slen >= max)
		ptr = max - slen;

	memcpy(buf + ptr, sign, slen);
	ptr += slen;

	ctx->sep = ptr;
	ctx->ptr = ptr;
	ctx->cur = ptr;

	ctx->show = 0;
	ctx->ends = ptr; /* to be updated later */
}
