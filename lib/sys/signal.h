#include <syscall.h>
#include <bits/types.h>
#include <bits/signal.h>

inline static long sys_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
	return syscall4(NR_rt_sigaction, signum, (long)act, (long)oldact, sizeof(sigset_t));
}

inline static long sys_sigprocmask(int how, const sigset_t* set, sigset_t* oldset)
{
	return syscall4(NR_rt_sigprocmask, how, (long)set, (long)oldset, sizeof(*set));
}

inline static long sys_kill(int pid, int sig)
{
	return syscall2(NR_kill, pid, sig);
}
