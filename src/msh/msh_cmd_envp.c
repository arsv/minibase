#include "msh.h"
#include "msh_cmd.h"

int cmd_setenv(struct sh* ctx, int argc, char** argv)
{
	int ret;

	if((ret = numargs(ctx, argc, 3, 3)))
		return ret;

	setenv(ctx, argv[1], argv[2]);

	return 0;
}

int cmd_unset(struct sh* ctx, int argc, char** argv)
{
	int i;
	int ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++)
		undef(ctx, argv[i]);

	return 0;
}

int cmd_export(struct sh* ctx, int argc, char** argv)
{
	int i;
	int ret;

	if((ret = numargs(ctx, argc, 2, 0)))
		return ret;

	for(i = 1; i < argc; i++)
		if(export(ctx, argv[i]))
			return error(ctx, "undefined variable", argv[i], 0);

	return 0;
}
