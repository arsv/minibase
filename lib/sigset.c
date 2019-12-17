#include <bits/types.h>
#include <bits/signal.h>
#include <sigset.h>

/* These two are typically used together */

int sigemptyset(struct sigset *set)
{
#if NSIGWORDS == 1
	set->word[0] = 0;
#elif NSIGWORDS == 2
	set->word[0] = 0;
	set->word[1] = 0;
#elif NSIGWORDS == 4
	set->word[0] = 0;
	set->word[1] = 0;
	set->word[2] = 0;
	set->word[3] = 0;
#else
# error unexpected NSIGWORD value
#endif
	return 0;
}

int sigaddset(struct sigset *set, int sig)
{
	if(sig < 1 || sig > SIGRTMAX)
		return -1;

	sig = sig - 1;

#if NSIGWORDS == 1
	set->word[0] |= (1 << sig);
#else
	uint bpw = 8*sizeof(long);
	set->word[sig/bpw] |= (1 << (sig % bpw));
#endif

	return 0;
}
