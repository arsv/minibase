#include <syscall.h>
#include <bits/signal.h>

inline static long syssigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	return syscall4(__NR_rt_sigprocmask, how, (long)set, (long)oldset, sizeof(*set));
}
