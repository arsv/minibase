#include <config.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("loadmod");

int main(int argc, char** argv)
{
	int ret;

	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	char* name = argv[1];

	struct mbuf mb;
	struct upac pac = {
		.envp = argv + argc + 1,
		.sdir = BASE_ETC "/pac"
	};

	if((ret = load_module(&pac, &mb, name)) < 0)
		return -1;

	warn("loaded successfully", NULL, 0);

	return 0;
}
