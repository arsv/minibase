#ifndef __BITS_SIGNAL_H__
#define __BITS_SIGNAL_H__

#define SIGHUP           1
#define SIGINT           2
#define SIGQUIT          3
#define SIGILL           4
#define SIGTRAP          5
#define SIGABRT          6
#define SIGEMT           7
#define SIGFPE           8
#define SIGKILL          9
#define SIGBUS          10
#define SIGSEGV         11
#define SIGSYS          12
#define SIGPIPE         13
#define SIGALRM         14
#define SIGTERM         15
#define SIGUSR1         16
#define SIGUSR2         17
#define SIGCHLD         18
#define SIGPWR          19
#define SIGWINCH        20
#define SIGURG          21
#define SIGIO           22
#define SIGSTOP         23
#define SIGTSTP         24
#define SIGCONT         25
#define SIGTTIN         26
#define SIGTTOU         27
#define SIGVTALRM       28
#define SIGPROF         29
#define SIGXCPU         30
#define SIGXFSZ         31

#define SIGRTMIN        32
#define SIGRTMAX        63

#define NSIGWORDS 4

struct sigset {
	unsigned long word[4];
};

#define SIG_BLOCK       1
#define SIG_UNBLOCK     2
#define SIG_SETMASK     3

#define SIG_DFL ((void*) 0L)
#define SIG_IGN ((void*) 1L)
#define SIG_ERR ((void*)~0L)

#define SA_ONSTACK      0x08000000
#define SA_RESETHAND    0x80000000
#define SA_RESTART      0x10000000
#define SA_SIGINFO      0x00000008
#define SA_NODEFER      0x40000000
#define SA_NOCLDWAIT    0x00010000
#define SA_NOCLDSTOP    0x00000001

struct sigaction {
	unsigned int flags;
	union {
		void (*action)(int, void*, void*);
		void (*handler)(int);
	};
	struct sigset mask;
};

#define SIGHANDLER(sa, hh, fl) \
	struct sigaction sa = { \
		.handler = hh, \
		.flags = fl, \
		.mask = { { 0 } } \
	}

#endif
