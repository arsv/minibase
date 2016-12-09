#include <bits/types.h>
#include <syscall.h>

inline static long sysmremap(void* old, size_t oldsize, size_t newsize, int flags)
{
	return syscall4(__NR_mremap, (long)old, oldsize, newsize, flags);
}
