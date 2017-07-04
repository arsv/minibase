#include <sys/kill.h>
#include <sys/pid.h>

/* libgcc relies on this function in some cases */
/* (ARM: signal FPE on divide by zero) */

int raise(int sig)
{
	return sys_kill(sys_getpid(), sig);
}
