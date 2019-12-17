#ifndef __BITS_SIGNAL_H__
#define __BITS_SIGNAL_H__

#define SIGHUP           1
#define SIGINT           2
#define SIGQUIT          3
#define SIGILL           4
#define SIGTRAP          5
#define SIGABRT          6
#define SIGIOT           6
#define SIGFPE           8
#define SIGKILL          9
#define SIGSEGV         11
#define SIGPIPE         13
#define SIGALRM         14
#define SIGTERM         15
#define SIGUNUSED       31
#define SIGBUS           7
#define SIGUSR1         10
#define SIGUSR2         12
#define SIGSTKFLT       16
#define SIGCHLD         17
#define SIGCONT         18
#define SIGSTOP         19
#define SIGTSTP         20
#define SIGTTIN         21
#define SIGTTOU         22
#define SIGURG          23
#define SIGXCPU         24
#define SIGXFSZ         25
#define SIGVTALRM       26
#define SIGPROF         27
#define SIGWINCH        28
#define SIGIO           29
#define SIGPWR          30
#define SIGSYS          31

#define SIGRTMIN        32
#define SIGRTMAX        63

#define NSIGWORDS 1

struct sigset {
	unsigned long word[1];
};

#define SIG_BLOCK       0
#define SIG_UNBLOCK     1
#define SIG_SETMASK     2

#define SIG_DFL ((void*) 0L)
#define SIG_IGN ((void*) 1L)
#define SIG_ERR ((void*)~0L)

#define SA_NOCLDSTOP    (1<<0)
#define SA_NOCLDWAIT    (1<<1)
#define SA_SIGINFO      (1<<2)
#define SA_RESTORER     (1<<26)
#define SA_ONSTACK      (1<<27)
#define SA_RESTART      (1<<28)
#define SA_NODEFER      (1<<30)
#define SA_RESETHAND    (1<<31)

struct sigaction {
	union {
		void (*action)(int, void*, void*);
		void (*handler)(int);
	};
	unsigned long flags;
	void (*restorer)(void);
	struct sigset mask;
};

#define SIGHANDLER(sa, hh, fl) \
	struct sigaction sa = { \
		.handler = hh, \
		.flags = fl | SA_RESTORER, \
		.restorer = sigreturn, \
		.mask = { { 0 } } \
	}

extern void sigreturn(void);

#endif
