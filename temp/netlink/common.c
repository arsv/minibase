#include <sys/file.h>
#include <sys/mman.h>

#include <util.h>

#include "common.h"

#define PAGE 4096
#define MAX_FILE_SIZE 0x50000

void* mmapwhole(const char* name, ulong* len)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);

	void* ptr = sys_mmap(NULL, st.size, PROT_READ, MAP_SHARED, fd, 0);

	if(mmap_error(ptr))
		fail("cannot mmap", name, (long)ptr);

	if(st.size > MAX_FILE_SIZE)
		fail("file too large:", name, ret);

	*len = st.size;
	return ptr;
}

void* readwhole(int fd, ulong* len)
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
