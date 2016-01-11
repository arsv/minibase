#include <bits/syscall.h>
#include <syscall.h>

inline static long sysmount(const char* source, const char* target,
		const char* fstype, long flags, const void* data)
{
	return syscall5(__NR_mount, (long)source, (long)target,
			(long)fstype, flags, (long)data);
}
