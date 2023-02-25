#include <sys/module.h>
#include <sys/info.h>
#include <sys/file.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "modprobe.h"

ERRTAG("modprobe");

int error(CTX, const char* msg, char* arg, int err)
{
	warn(msg, arg, err);

	return err ? err : -1;
}

char** environ(CTX)
{
	return ctx->envp;
}

static void dump_line(struct line* ln)
{
	int len = ln->end - ln->ptr;
	char* buf = alloca(len + 9);
	char* q = ln->ptr;
	int i;

	for(i = 0; i < len; i++) {
		char* z = q + i;
		char m;

		if(*z < 0x20)
			m = '.';
		else
			m = *z;

		buf[i] = m;
	}

	buf[i] = '\n';

	writeall(STDOUT, buf, len + 1);

	for(i = 0; i < len; i++) {
		char* z = q + i;
		char m;

		if(z == ln->sep)
			m = '^';
		else if(z == ln->val)
			m = '^';
		else
			m = ' ';

		buf[i] = m;
	}

	buf[i] = '\n';

	writeall(STDOUT, buf, len + 1);
}

static int prep_config(CTX)
{
	struct mbuf* mb = &ctx->config;
	char* name = BASE_ETC "/modules";

	return mmap_whole(mb, name, REQ);
}

static void try_mod_params(CTX)
{
	int ret;
	struct mbuf* mb = &ctx->config;
	struct line ln;
	char* name = "e1000e";

	if((ret = prep_config(ctx)) < 0)
		fail(NULL, NULL, ret);

	ret = locate_opt_line(mb, &ln, name);

	tracef("ret=%i\n", ret);

	dump_line(&ln);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	try_mod_params(ctx);

	return 0;
}
