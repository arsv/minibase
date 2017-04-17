#include <sys/stat.h>
#include <string.h>

#include "msh.h"
#include "msh_cmd.h"

static int cond_defined(struct sh* ctx, char** args)
{
	return valueof(ctx, args[0]) != 0 ? 0 : 1;
}

static int cond_exists(struct sh* ctx, char** args)
{
	struct stat st;

	return sysstat(args[0], &st) >= 0 ? 0 : 1;
}

static const struct cond {
	char name[8];
	int (*func)(struct sh* ctx, char** args);
	int nargs;
} conds[] = {
	{ "defined",  cond_defined, 1 },
	{ "exists",   cond_exists,  1 },
	{ "",         NULL,         0 }
};

static const struct cond* findcond(char* name)
{
	const struct cond* cd;

	for(cd = conds; cd->func; cd++)
		if(!strncmp(cd->name, name, sizeof(cd->name)))
			return cd;

	return NULL;
}

static int condition(struct sh* ctx, int argc, char** argv)
{
	const struct cond* cd;

	if(argc < 2)
		return error(ctx, "missing condition", NULL, 0);
	if(!(cd = findcond(argv[1])))
		return error(ctx, "unknown condition", argv[1], 0);

	if(argc != 2 + cd->nargs)
		return error(ctx, "invalid condition", NULL, 0);

	return cd->func(ctx, argv + 2);
}

int cmd_if(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;
	int prev = cond;
	int ret;

	cond = (cond << CSHIFT) | CHADIF;

	if(prev & CGUARD)
		return error(ctx, "too many nested conditionals", NULL, 0);
	if(prev & CSKIP)
		cond |= CSKIP;
	else if((ret = condition(ctx, argc, argv)) < 0)
		return ret;
	else if(ret == 0)
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;

	return 0;
}

int cmd_elif(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;
	int ret;

	if(!(cond & CHADIF))
		return error(ctx, "misplaced elif", NULL, 0);
	if((cond >> CSHIFT) & CSKIP)
		cond |= CSKIP;
	else if(cond & CHADTRUE)
		cond |= CSKIP;
	else if((ret = condition(ctx, argc, argv)) < 0)
		return ret;
	else if(ret == 0)
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;

	return 0;
}

int cmd_else(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;

	if(argc > 1)
		return error(ctx, "no arguments allowed", NULL, 0);
	if(!(cond & CHADIF) || (cond & CHADELSE))
		return error(ctx, "misplaced else", NULL, 0);
	if((cond >> CSHIFT) & CSKIP)
		cond |= CSKIP;
	else if(cond & CHADTRUE)
		cond |= CSKIP;
	else
		cond |= CHADELSE;

	ctx->cond = cond;

	return 0;
}

int cmd_fi(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;

	if(argc > 1)
		return error(ctx, "no arguments allowed", NULL, 0);
	if(!(cond & CHADIF))
		return error(ctx, "misplaced fi", NULL, 0);

	ctx->cond = (cond >> CSHIFT);

	return 0;
}
