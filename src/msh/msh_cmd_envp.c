#include "msh.h"
#include "msh_cmd.h"

void cmd_setenv(CTX)
{
	char* var = shift(ctx);
	char* val = shift(ctx);

	no_more_arguments(ctx);

	setenv(ctx, var, val);
}

void cmd_unset(CTX)
{
	char* var;

	need_some_arguments(ctx);

	while((var = next(ctx)))
		undef(ctx, var);
}

void cmd_export(CTX)
{
	char* var;

	need_some_arguments(ctx);

	while((var = next(ctx)))
		export(ctx, var);
}
