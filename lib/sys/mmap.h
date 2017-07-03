#include <syscall.h>
#include <bits/types.h>
#include <bits/mman.h>

inline static int mmap_error(long ret)
{
	return (ret < 0 && ret > -2048);
}

inline static long sys_mmap(void* addr, size_t length, int prot, int flags,
                            int fd, size_t offset)
{
	return syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

inline static long sys_mremap(void* old, size_t oldsize, size_t newsize, int flags)
{
	return syscall4(__NR_mremap, (long)old, oldsize, newsize, flags);
}

inline static long sys_munmap(void* ptr, unsigned long len)
{
	return syscall2(__NR_munmap, (long)ptr, len);
}
