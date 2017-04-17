#include "msh.h"
#include "msh_cmd.h"

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
