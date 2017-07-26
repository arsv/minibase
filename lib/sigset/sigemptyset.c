#include <sigset.h>

int sigemptyset(sigset_t *set)
{
#if BITS == 32
	set->low = 0;
	set->high = 0;
#elif BITS == 64
	*set = 0;
#else
# error unexpected BITS value
#endif
	return 0;
}

