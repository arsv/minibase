#include <sys/deletemodule.h>

#include <null.h>
#include <fail.h>

ERRTAG = "rmmod";
ERRLIST = {
	REPORT(EBUSY), REPORT(EFAULT), REPORT(ENOENT),
	REPORT(EPERM), REPORT(EWOULDBLOCK), RESTASNUMBERS
};

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
	int flags = O_NONBLOCK;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		flags = applyopts(flags, argv[i++] + 1);

	if(i >= argc)
		fail("module name required", NULL, 0);

	for(; i < argc; i++)
		xchk( sysdeletemodule(argv[i], flags),
			"cannot remove", argv[i] );

	return 0;
}
