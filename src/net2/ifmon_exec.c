#include <sys/proc.h>
#include <sys/signal.h>

#include <format.h>
#include <printf.h>
#include <string.h>
#include <util.h>

#include "ifmon.h"

void stop_link_procs(struct link* ls, int drop)
{
	struct proc* ch;
	int ifi = ls->ifi;

	for(ch = procs; ch < procs + nprocs; ch++) {
		if(ch->ifi != ifi)
			continue;
		if(ch->pid <= 0)
			continue;

		sys_kill(ch->pid, SIGTERM);

		if(!drop) continue;

		ch->ifi = 0;
	}
}

int any_pids_left(void)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ch->pid > 0)
			return 1;

	return 0;
}

void kill_all_procs(struct link* ls)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ls && ls->ifi != ch->ifi)
			;
		else if(ch->pid <= 0)
			;
		else sys_kill(ch->pid, SIGTERM);
}

int kill_tagged(struct link* ls, int tag)
{
	struct proc* ch;
	int count = 0;

	for(ch = procs; ch < procs + nprocs; ch++) {
		if(ls->ifi != ch->ifi)
			continue;
		if(ch->pid <= 0)
			continue;
		if(ch->tag != tag)
			continue;

		sys_kill(ch->pid, SIGTERM);
		count++;
	}

	return count;
}

int any_procs_left(struct link* ls)
{
	struct proc* ch;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ls && ls->ifi != ch->ifi)
			continue;
		else if(ch->pid > 0)
			return 1;

	return 0;
}

void waitpids(void)
{
	struct proc* ch;
	struct link* ls;
	int pid;
	int status;
	int ifi;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(!(ch = find_proc_slot(pid)))
			continue;
		if((ifi = ch->ifi) < 0)
			continue;

		int tag = ch->tag;

		free_proc_slot(ch);

		if(!(ls = find_link_slot(ifi)))
			continue;

		link_exit(ls, tag, status);
	}
}

static int any_running(struct link* ls, int tag)
{
	struct proc* ch;
	int ifi = ls->ifi;

	for(ch = procs; ch < procs + nprocs; ch++)
		if(ch->ifi == ifi && ch->tag == tag)
			return 1;

	return 0;
}

int spawn(struct link* ls, int tag, char** args)
{
	struct proc* ch;
	int pid;
	int ret;

	tracef("spawn %s %s\n", args[0], args[1]);

	if(any_running(ls, tag))
		return -EALREADY;
	if(!(ch = grab_proc_slot()))
		return -ENOMEM;
	if((pid = sys_fork()) < 0)
		return pid;

	if(pid == 0) {
		ret = execvpe(*args, args, environ);
		warn("exec", *args, ret);
		_exit(0xFF);
	}

	ch->ifi = ls->ifi;
	ch->pid = pid;
	ch->tag = tag;

	return 0;
}
