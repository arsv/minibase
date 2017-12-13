#include <syscall.h>
#include <bits/types.h>
#include <bits/mman.h>

inline static int mmap_error(void* ptr)
{
	long ret = (long)ptr;

	if(ret >= 0)
		return 0;
	if(ret < -2048)
		return 0;

	return (int)ret;
}

inline static int brk_error(void* brk, void* end)
{
	int ret;

	if((ret = mmap_error(brk)))
		return ret;
	if((ret = mmap_error(end)))
		return ret;
	if(end <= brk)
		return -EINVAL;

	return 0;
}

inline static size_t pagealign(size_t sz)
{
	return ((sz + PAGE - 1) & ~(PAGE - 1));
}

inline static void* sys_brk(void* ptr)
{
	return (void*)syscall1(NR_brk, (long)ptr);
}

inline static void* sys_mmap(void* addr, size_t length, int prot, int flags, int fd, size_t offset)
{
	return (void*)syscall6(NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

inline static void* sys_mremap(void* old, size_t oldsize, size_t newsize, int flags)
{
	return (void*)syscall4(NR_mremap, (long)old, oldsize, newsize, flags);
}

inline static long sys_munmap(void* ptr, unsigned long len)
{
	return syscall2(NR_munmap, (long)ptr, len);
}
