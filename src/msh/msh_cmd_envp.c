#include "msh.h"
#include "msh_cmd.h"

int cmd_setenv(CTX)
{
	char *var, *val;

	if(shift_str(ctx, &var))
		return -1;
	if(shift_str(ctx, &val))
		return -1;
	if(moreleft(ctx))
		return -1;

	setenv(ctx, var, val);

	return 0;
}

int cmd_unset(CTX)
{
	char* var;

	if(noneleft(ctx))
		return -1;
	while((var = shift(ctx)))
		undef(ctx, var);

	return 0;
}

int cmd_export(CTX)
{
	char* var;

	if(noneleft(ctx))
		return -1;
	while((var = shift(ctx)))
		if(export(ctx, var))
			return error(ctx, "undefined variable", var, 0);

	return 0;
}
