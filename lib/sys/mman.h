#include <syscall.h>
#include <bits/types.h>
#include <bits/mman.h>

inline static int mmap_error(void* ptr)
{
	long ret = (long)ptr;

	return (ret < 0 && ret > -2048);
}

inline static int brk_error(void* brk, void* end)
{
	if(mmap_error(brk))
		return 1;
	if(mmap_error(end))
		return 1;
	if(end <= brk)
		return 1;

	return 0;
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
