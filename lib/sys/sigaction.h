#include <syscall.h>
#include <bits/signal.h>

inline static long syssigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	return syscall3(__NR_rt_sigaction, signum, (long)act, (long)oldact);
}
