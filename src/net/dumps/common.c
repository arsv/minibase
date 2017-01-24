#include <sys/fstat.h>
#include <sys/open.h>
#include <sys/mmap.h>

#include <null.h>
#include <fail.h>

#include "common.h"

#define MAX_FILE_SIZE 0x50000

ERRLIST = {
	REPORT(ENOENT), REPORT(ENOMEM), REPORT(EACCES), REPORT(EPERM),
	REPORT(ENOTDIR), REPORT(EISDIR), REPORT(EINVAL), REPORT(EFAULT),
	RESTASNUMBERS
};

void* mmapwhole(const char* name, long* len)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sysopen(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sysfstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	ret = sysmmap(NULL, st.st_size, prot, flags, fd, 0);

	if(MMAPERROR(ret))
		fail("cannot mmap", name, ret);

	if(st.st_size > MAX_FILE_SIZE)
		fail("file too large:", name, ret);	

	*len = st.st_size;
	return (void*) ret;
}
