#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <main.h>
#include <util.h>

ERRTAG("mmap");

static void test_anonymous(void)
{
	int ret;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int size = 2*PAGE;

	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		fail(NULL, "PRIVATE", ret);

	memset(buf, 'x', size);

	if((ret = sys_munmap(buf, size)) < 0)
		fail("munmap", NULL, ret);
}

static void test_filebacked(void)
{
	int fd, ret;
	char* name = "mmap";
	struct stat st;

	int prot = PROT_READ;
	int flags = MAP_SHARED;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(name, name, fd);

	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	void* buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail(NULL, name, ret);

	if(memcmp(buf, "\x7F""ELF", 4))
		fail("no ELF header in", name, 0);

	if((ret = sys_munmap(buf, st.size)) < 0)
		fail("munmap", name, ret);
}


int main(noargs)
{
	test_anonymous();
	test_filebacked();

	return 0;
}
