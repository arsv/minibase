#include <sys/kill.h>
#include <sys/alarm.h>
#include <sys/write.h>
#include <sys/waitpid.h>
#include <sys/close.h>

#include <null.h>

#include "vtmux.h"

/* Non-terminal SIGCHLD handler. */

static struct vtx* find_pid_rec(int pid)
{
	int i;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].pid == pid)
			return &consoles[i];

	return NULL;
}

static void report_cause(int fd, int status)
{
	syswrite(fd, "blah blah blah\n", 15);
}

void waitpids(void)
{
	int status;
	int pid;
	struct vtx* active = NULL;

	while((pid = syswaitpid(-1, &status, WNOHANG)) > 0)
	{
		struct vtx* cvt = find_pid_rec(pid);

		if(!cvt)
			continue;
		if(status)
			report_cause(cvt->ttyfd, status);
		if(cvt->tty == activetty && !status)
			active = cvt;

		closevt(cvt, !!status);
	}

	request_fds_update();

	if(!active)
		return;
	if(active->fix)
		switchto(active->tty); /* try to restart it */
	else
		switchto(consoles[0].tty); /* greeter */
}

/* Shutdown routines: wait for VT clients to die before exiting. */

static int countrunning(void)
{
	int i;
	int count = 0;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].pid > 0)
			count++;

	return count;
}

static void markdead(int pid)
{
	int i;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].pid == pid)
			consoles[i].pid = -1;
}

static void killall(void)
{
	int i;

	for(i = 0; i < nconsoles; i++)
		if(consoles[i].pid > 0)
			syskill(consoles[i].pid, SIGTERM);
}

void shutdown(void)
{
	int status;
	int pid;

	sysalarm(5);
	killall();

	while(countrunning() > 0)
		if((pid = syswaitpid(-1, &status, 0)) > 0)
			markdead(pid);
		else break;

	unlock_switch();
}
