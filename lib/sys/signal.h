#include <syscall.h>
#include <bits/signal.h>

#define SIG_BLOCK	0
#define SIG_UNBLOCK	1
#define SIG_SETMASK	2

#define SIG_DFL ((void*) 0L)
#define SIG_IGN ((void*) 1L)
#define SIG_ERR ((void*)~0L)

extern void sigreturn(void);

inline static long sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	return syscall4(__NR_rt_sigaction, signum, (long)act, (long)oldact, sizeof(sigset_t));
}

inline static long sys_sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	return syscall4(__NR_rt_sigprocmask, how, (long)set, (long)oldset, sizeof(*set));
}

inline static long sys_kill(int pid, int sig)
{
	return syscall2(__NR_kill, pid, sig);
}
