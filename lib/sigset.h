#ifndef SIGNAL_H
#define SIGNAL_H

#include <bits/types.h>
#include <bits/signal.h>

int sigemptyset(struct sigset *set);
int sigaddset(struct sigset *set, int signum);

#endif
