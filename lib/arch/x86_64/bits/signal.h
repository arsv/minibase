#ifndef __BITS_SIGNAL_H__
#define __BITS_SIGNAL_H__

#define _NSIG		64
#define NSIG		32
#define SIGRTMAX	(_NSIG-1)
#define MINSIGSTKSZ	2048
#define _NSIG_WORDS	((_NSIG/sizeof(long))>>3)

#define SIGHUP		 1
#define SIGINT		 2
#define SIGQUIT		 3
#define SIGILL		 4
#define SIGTRAP		 5
#define SIGABRT		 6
#define SIGIOT		 6
#define SIGFPE		 8
#define SIGKILL		 9
#define SIGSEGV		11
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGUNUSED	31
#define SIGBUS		 7
#define SIGUSR1		10
#define SIGUSR2		12
#define SIGSTKFLT	16
#define SIGCHLD		17
#define SIGCONT		18
#define SIGSTOP		19
#define SIGTSTP		20
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGURG		23
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGIO		29
#define SIGPWR		30
#define SIGSYS		31

#define SIGCLD		SIGCHLD
#define SIGPOLL		SIGIO

#define SIGLOST		SIGPWR
#define SIGRTMIN	32
#define SIGRTMAX	(_NSIG-1)

/* SA_FLAGS values: */
#define SA_NOCLDSTOP	0x00000001
#define SA_NOCLDWAIT	0x00000002 /* not supported yet */
#define SA_SIGINFO	0x00000004
#define SA_RESTORER	0x04000000
#define SA_ONSTACK	0x08000000
#define SA_RESTART	0x10000000
#define SA_INTERRUPT	0x20000000 /* dummy -- ignored */
#define SA_NODEFER	0x40000000
#define SA_RESETHAND	0x80000000

/* sigaltstack controls */
#define SS_ONSTACK	1
#define SS_DISABLE	2

#define MINSIGSTKSZ	2048
#define SIGSTKSZ	8192

#define SIG_BLOCK	0	/* for blocking signals */
#define SIG_UNBLOCK	1	/* for unblocking signals */
#define SIG_SETMASK	2	/* for setting the signal mask */

#define SIG_DFL ((sighandler_t)0L)	/* default signal handling */
#define SIG_IGN ((sighandler_t)1L)	/* ignore signal */
#define SIG_ERR ((sighandler_t)-1L)	/* error return from signal */

typedef void (*sighandler_t)(int);
typedef struct siginfo siginfo_t;

typedef struct {
  unsigned long sig[_NSIG_WORDS];
} sigset_t;

struct sigaction {
  union {
    sighandler_t _sa_handler;
    void (*_sa_sigaction)(int, siginfo_t*, void*);
  } _u;
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  sigset_t sa_mask;
};

#define sa_handler	_u._sa_handler
#define sa_sigaction	_u._sa_sigaction

#endif
