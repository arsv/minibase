#include <sys/module.h>

#include <alloca.h>
#include <format.h>
#include <util.h>
#include <fail.h>

#include "kmod.h"

ERRTAG = "kmod";
ERRLIST = {
	REPORT(EACCES), REPORT(EAGAIN), REPORT(EBADF), REPORT(EINVAL),
	REPORT(ENFILE), REPORT(ENODEV), REPORT(ENOMEM), REPORT(EPERM),
	REPORT(ETXTBSY), REPORT(EOVERFLOW), REPORT(EBADMSG), REPORT(EBUSY),
	REPORT(EFAULT), REPORT(ENOKEY), REPORT(EEXIST), REPORT(ENOEXEC),
	RESTASNUMBERS
};

#define OPTS "r"
#define OPT_r (1<<0)	/* rmmod */

static int looks_like_path(char* name)
{
	char* p;

	for(p = name; *p; p++)
		if(*p == '/' || *p == '.')
			return 1;

	return 0;
}

static void rmmod(char* name)
{
	xchk(sys_delete_module(name, 0), name, NULL);
}

static void decidepars(char* module, int argn, char** args, char** envp)
{
	int parlen = argsumlen(argn, args) + argn;
	char* pars = alloca(parlen + 1);
	char* pend = argsmerge(pars, pars + parlen, argn, args);
	*pend = '\0';

	if(looks_like_path(module))
		insmod(module, pars, envp);
	else
		modprobe(module, pars, envp);
}

int main(int argc, char** argv, char** envp)
{
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i >= argc)
		fail("module name required", NULL, 0);

	if(opts & OPT_r) {
		for(; i < argc; i++)
			rmmod(argv[i]);
	} else {
		char* module = argv[i++];

		argc -= i;
		argv += i;

		decidepars(module, argc, argv, envp);
	};

	return 0;
}
