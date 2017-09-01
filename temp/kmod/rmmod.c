#include <sys/module.h>

#include <errtag.h>
#include <util.h>

ERRTAG("rmmod");
ERRLIST(NEBUSY NEFAULT NENOENT NEPERM NEWOULDBLOCK);

static int applyopts(int flags, const char* keys)
{
	const char *p;
	char opt[] = "-?";

	for(p = keys; *p; p++)
		switch(*p) {
			case 'f': flags |= O_TRUNC; break;
			case 'w': flags &= ~O_NONBLOCK; break;
			default:
				opt[1] = *p;
				fail("unknown option", opt, 0);
		}

	return flags;
}

int main(int argc, char** argv)
{
	int ret;
	int flags = O_NONBLOCK;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		flags = applyopts(flags, argv[i++] + 1);

	if(i >= argc)
		fail("module name required", NULL, 0);

	for(; i < argc; i++)
		if((ret = sys_delete_module(argv[i], flags)) < 0)
			fail("cannot remove", argv[i], ret);

	return 0;
}
