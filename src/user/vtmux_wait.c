#include <sys/kill.h>
#include <sys/alarm.h>
#include <sys/waitpid.h>
#include <sys/close.h>

#include "vtmux.h"

/* Non-terminal SIGCHLD handler. */

void waitpids(void)
{
	int status;
	int pid;

	while((pid = syswaitpid(-1, &status, WNOHANG)) > 0)
		close_dead_client(pid);
}

/* Shutdown routines: wait for VTs to die before exiting. */
   
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
}
