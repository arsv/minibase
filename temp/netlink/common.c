#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mmap.h>
#include <sys/brk.h>

#include <null.h>
#include <fail.h>

#include "common.h"

#define PAGE 4096
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

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	ret = sys_mmap(NULL, st.st_size, prot, flags, fd, 0);

	if(mmap_error(ret))
		fail("cannot mmap", name, ret);

	if(st.st_size > MAX_FILE_SIZE)
		fail("file too large:", name, ret);	

	*len = st.st_size;
	return (void*) ret;
}

void* readwhole(int fd, long* len)
{
	char* brk = (char*)sys_brk(NULL);
	char* end = (char*)sys_brk(brk + PAGE);
	char* ptr = brk;

	if(end < ptr + PAGE)
		fail("out of memory", NULL, 0);

	long rd;

	while((rd = sys_read(fd, ptr, end - ptr)) > 0) {
		ptr += rd;

		if(end - ptr < 100)
			end = (char*)sys_brk(end + PAGE);
		if(end - ptr < 100)
			fail("out of memory", NULL, 0);
	}

	*len = ptr - brk;
	return brk;
}
