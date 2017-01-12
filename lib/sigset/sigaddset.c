#include <sigset.h>

int sigaddset(sigset_t *set, int sig)
{
	if(sig < 1 || sig > SIGRTMAX)
		return -1;
	else
		sig--;

	const int bpl = 8*sizeof(long); /* bits per long */

	set->sig[sig/bpl] |= (1U << (sig % bpl));

	return 0;
}
