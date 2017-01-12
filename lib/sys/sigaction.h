#include <syscall.h>
#include <bits/signal.h>

extern void sigreturn(void);

inline static long syssigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	return syscall4(__NR_rt_sigaction, signum, (long)act, (long)oldact, sizeof(sigset_t));
}
