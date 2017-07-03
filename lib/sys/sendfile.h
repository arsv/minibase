#include <syscall.h>
#include <bits/types.h>
#include <bits/stdio.h>

inline static long sys_sendfile(int ofd, int ifd, uint64_t* offset, size_t count)
{
	return syscall4(__NR_sendfile, ofd, ifd, (long)offset, count);
}
