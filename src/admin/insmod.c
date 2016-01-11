#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <sys/initmodule.h>

#include <null.h>
#include <fail.h>
#include <alloca.h>
#include <strlen.h>
#include <basename.h>
#include <argsmerge.h>

ERRTAG = "insmod";
ERRLIST = {
	REPORT(EACCES), REPORT(EAGAIN), REPORT(EBADF), REPORT(EINVAL),
	REPORT(ENFILE), REPORT(ENODEV), REPORT(ENOMEM), REPORT(EPERM),
	REPORT(ETXTBSY), REPORT(EOVERFLOW), REPORT(EBADMSG), REPORT(EBUSY),
	REPORT(EFAULT), REPORT(ENOKEY), REPORT(EEXIST), REPORT(ENOEXEC),
	RESTASNUMBERS
};

static void* mmapmodule(const char* name, unsigned long* len)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sysopen(name, O_RDONLY)) < 0)
		fail("cannot open", name, -fd);

	if((ret = sysfstat(fd, &st)) < 0)
		fail("cannot stat", name, -ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	ret = sysmmap(NULL, st.st_size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		fail("cannot mmap", name, -ret);

	*len = st.st_size;
	return (void*) ret;
}

static int loadmodule(const char* name, const char* pars)
{
	const char* base = basename(name);

	unsigned long len;
	void* mod = mmapmodule(name, &len);

	long ret = sysinitmodule(mod, len, pars);

	if(ret < 0)
		fail("kernel rejects", base, -ret);

	return 0;
};

int main(int argc, char** argv)
{
	if(argc < 2)
		fail("module name required", NULL, 0);

	char* name = argv[1];

	argc -= 2;
	argv += 2;

	int parlen = argsumlen(argc, argv) + argc;
	char* pars = alloca(parlen + 1);
	char* pend = argsmerge(pars, pars + parlen, argc, argv);
	*pend = '\0';

	return loadmodule(name, pars);
}
