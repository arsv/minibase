#include <bits/ints.h>
#include <bits/signal.h>
#include <sigset.h>

int sigaddset(sigset_t *set, int sig)
{
	if(sig < 1 || sig > SIGRTMAX)
		return -1;

#if BITS == 32
	if(sig < 32)
		set->low |= (1 << (sig - 1));
	else
		set->high |= (1 << (sig - 32 - 1));
#elif BITS == 64
	*set |= (1 << (sig - 1));
#else
# error unexpected BITS value
#endif

	return 0;
}
