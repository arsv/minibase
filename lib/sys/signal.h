#include <syscall.h>
#include <bits/signal.h>

extern void sigreturn(void);

inline static long sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	return syscall4(__NR_rt_sigaction, signum, (long)act, (long)oldact, sizeof(sigset_t));
}

inline static long sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	return syscall4(__NR_rt_sigprocmask, how, (long)set, (long)oldset, sizeof(*set));
}
