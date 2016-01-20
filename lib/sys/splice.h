#include <syscall.h>
#include <bits/splice.h>
#include <bits/types.h>
#include <bits/stdio.h>

inline static long syssplice(int fdin, uint64_t* offin,
		int fdout, uint64_t* offout, unsigned long len, unsigned flags)
{
	return syscall6(__NR_splice,
			fdin, (long)offin, fdout, (long)offout, len, flags);
}
