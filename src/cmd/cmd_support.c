#include <sys/fpath.h>
#include <string.h>
#include <util.h>
#include "cmd.h"

noreturn void exit(CTX, int code)
{
	fini_input(ctx);
	_exit(code);
}

noreturn void quit(CTX, const char* msg, char* arg, int err)
{
	fini_input(ctx);
	fail(msg, arg, err);
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
