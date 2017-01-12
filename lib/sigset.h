#ifndef SIGNAL_H
#define SIGNAL_H

#include <bits/types.h>
#include <bits/signal.h>

//int kill(pid_t pid, int sig);

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signum);
int sigdelset(sigset_t *set, int signum);
int sigismember(const sigset_t *set, int signo);
//int sigsuspend(const sigset_t *mask);
//int sigpending(sigset_t *set);
//int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
//int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);

#endif
