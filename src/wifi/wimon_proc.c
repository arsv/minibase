#include <sys/waitpid.h>

#include "wimon.h"

void waitpids(void)
{
	int pid;
	int status;

	while((pid = syswaitpid(-1, &status, WNOHANG)) > 0) {

	}
}
