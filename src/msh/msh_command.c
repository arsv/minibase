#include <sys/file.h>
#include <sys/proc.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct cmd {
	char name[12];
	int (*func)(CTX);
} builtins[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
};

static const struct cmd* builtin(const char* name)
{
	const struct cmd* cc;
	int maxlen = sizeof(cc->name);

	for(cc = builtins; cc < ARRAY_END(builtins); cc++)
		if(!strncmp(cc->name, name, maxlen))
			return cc;

	return NULL;
}

void command(CTX)
{
	const struct cmd* cc;

	if(!ctx->argc)
		return;

	char* lead = ctx->argv[0];

	if(!(cc = builtin(lead)))
		fatal(ctx, "unknown command", lead);

	if(!(cc->func(ctx)))
		return;

	exit(ctx, 0xFF);
}
