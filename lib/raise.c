#include <sys/kill.h>
#include <sys/getpid.h>

/* libgcc relies on this function in some cases */
/* (ARM: signal FPE on divide by zero) */

int raise(int sig)
{
	return syskill(sysgetpid(), sig);
}
