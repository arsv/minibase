#include <syscall.h>
#include <bits/types.h>
#include <bits/stdio.h>

#define SPLICE_F_MOVE      (1<<0)
#define SPLICE_F_NONBLOCK  (1<<1)
#define SPLICE_F_MORE      (1<<2)
#define SPLICE_F_GIFT      (1<<3)

inline static long sys_splice(int fdin, uint64_t* offin, int fdout,
                              uint64_t* offout, size_t len, unsigned flags)
{
	return syscall6(__NR_splice, fdin, (long)offin, fdout, (long)offout,
                                                                   len, flags);
}

inline static long sys_tee(int fdin, int fdout, size_t len, unsigned flags)
{
	return syscall4(__NR_tee, fdin, fdout, len, flags);
}

inline static long sys_sendfile(int ofd, int ifd, uint64_t* offset, size_t count)
{
	return syscall4(__NR_sendfile, ofd, ifd, (long)offset, count);
}
