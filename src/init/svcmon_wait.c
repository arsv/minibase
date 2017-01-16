#include <sys/waitpid.h>
#include <format.h>

#include "svcmon.h"

static void markdead(struct svcrec* rc, int status)
{
	rc->pid = 0;
	rc->status = status;

	if(rc->flags & P_STALE)
		droprec(rc);

	/* possibly unexpected death */
	gg.state |= S_PASSREQ;
}

void waitpids(void)
{
	pid_t pid;
	int status;
	struct svcrec *rc;
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
