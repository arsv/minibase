#include <null.h>

#include "msh.h"
#include "msh_cmd.h"

static int condition(struct sh* ctx, int argc, char** argv)
{
	return 0;
}

int cmd_if(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;
	int prev = cond;

	cond = (cond << CSHIFT) | CHADIF;

	if(prev & CGUARD)
		return error(ctx, "too many nested conditionals", NULL, 0);
	if(prev & CSKIP)
		cond |= CSKIP;
	else if(condition(ctx, argc, argv))
		cond |= CHADTRUE;
	else
		cond |= CSKIP;

	ctx->cond = cond;

	return 0;
}

int cmd_elif(struct sh* ctx, int argc, char** argv)
{
	int cond = ctx->cond;

	if(!(cond & CHADIF))
		return error(ctx, "misplaced elif", NULL, 0);
	if((cond >> CSHIFT) & CSKIP)
		cond |= CSKIP;
	else if(cond & CHADTRUE)
		cond |= CSKIP;
	else if(condition(ctx, argc, argv))
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
