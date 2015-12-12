#include <bits/mman.h>
#include <bits/errno.h>
#include <bits/fcntl.h>
#include <bits/stat.h>
#include <sys/open.h>
#include <sys/fstat.h>
#include <sys/mmap.h>
#include <sys/initmodule.h>

#include <null.h>
#include <fail.h>
#include <alloca.h>
#include <strlen.h>
#include <strapp.h>
#include <basename.h>

ERRTAG = "insmod";
ERRLIST = {
	REPORT(EACCES), REPORT(EAGAIN), REPORT(EBADF), REPORT(EINVAL),
	REPORT(ENFILE), REPORT(ENODEV), REPORT(ENOMEM), REPORT(EPERM),
	REPORT(ETXTBSY), REPORT(EOVERFLOW), REPORT(EBADMSG), REPORT(EBUSY),
	REPORT(EFAULT), REPORT(ENOKEY), REPORT(EEXIST), REPORT(ENOEXEC),
	RESTASNUMBERS
};

static int sumlen(int argc, char** argv)
{
	int i, len = 0;

	for(i = 0; i < argc; i++)
		len += strlen(argv[i]);

	return len;
};

static void mergeargs(char* buf, int len, int argc, char** argv)
{
	char* end = buf + len - 1;
	char* p = buf;
	int i;

	for(i = 0; i < argc; i++) {
		p = strapp(p, end, argv[i]);
		if(p >= end) break;
		*p++ = ' ';
	}
}

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

	int parlen = sumlen(argc, argv) + argc;
	char* pars = alloca(parlen);
	mergeargs(pars, parlen, argc, argv);

	return loadmodule(name, pars);
}
