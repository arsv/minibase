#include <sys/waitpid.h>
#include <format.h>

#include "init.h"

static void markdead(struct initrec* rc, int status)
{
	rc->pid = 0;
	rc->status = status;

	if(rc->flags & P_STALE)
		droprec(rc);
}

void waitpids(void)
{
	pid_t pid;
	int status;
	struct initrec *rc;
	const int flags = WNOHANG | WUNTRACED | WCONTINUED;

	while((pid = syswaitpid(-1, &status, flags)) > 0)
	{
		for(rc = firstrec(); rc; rc = nextrec(rc))
			if(rc->pid == pid)
				break;
		if(!rc)	/* Some stray child died. Like we care. */
			continue;

		if(WIFSTOPPED(status))
			rc->flags |= P_SIGSTOP;
		else if(WIFCONTINUED(status))
			rc->flags &= ~P_SIGSTOP;
		else
			markdead(rc, status);
	}
}
