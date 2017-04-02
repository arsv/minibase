#include <sys/waitpid.h>
#include <sys/execve.h>
#include <sys/fork.h>
#include <sys/kill.h>
#include <sys/_exit.h>
#include <format.h>
#include <util.h>
#include <null.h>
#include <fail.h>

#include "wimon.h"
#include "wimon_proc.h"
#include "wimon_slot.h"

struct wpaconf wp;

static void dump_spawn(struct link* ls, char** args)
{
	char** p;

	eprintf("spawn %s :", ls->name);
	for(p = args; *p; p++)
		eprintf(" %s", *p);
	eprintf("\n");
}

static void stop_link_procs(int ifi, int except)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->ifi != ifi)
			continue;
		else if(ch->pid <= 0 || ch->pid == except)
			continue;
		else
			syskill(ch->pid, SIGTERM);
}

static void deif_link_procs(int ifi)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->ifi == ifi)
			ch->ifi = -1;
}

static int any_link_procs(int ifi)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->ifi == ifi)
			return 1;

	return 0;
}

static void link_terminated(struct link* ls)
{
	eprintf("link_terminated %s\n", ls->name);
	ls->failed = 0;
}

static void terminate_link(struct link* ls)
{
	int ifi = ls->ifi;
	int procs = any_link_procs(ifi);
	int firstcall = !ls->failed;

	ls->failed = 1;

	if(procs && firstcall)
		stop_link_procs(ifi, 0);
	else if(procs)
		; /* wait until everything terminates via child_exit */
	else if(ls->state & S_IPADDR)
		flush_link_address(ifi); /* and wait link_lost_ip */
	else
		link_terminated(ls);
}

static void child_exit(struct link* ls, int pid, int abnormal)
{
	if(abnormal)
		eprintf("abnormal exit on link %s\n", ls->name);
	else
		eprintf("normal exit on link %s\n", ls->name);

	if(abnormal || ls->failed)
		terminate_link(ls);
}

void waitpids(void)
{
	struct child* ch;
	struct link* ls;
	int pid;
	int status;

	while((pid = syswaitpid(-1, &status, WNOHANG)) > 0) {
		if(!(ch = find_child_slot(pid)))
			continue;
		if((ls = find_link_slot(ch->ifi)))
			child_exit(ls, ch->pid, status);

		free_child_slot(ch);
	}
}

void spawn(struct link* ls, char** args, char** envp)
{
	struct child* ch;
	int pid;
	int ret;

	dump_spawn(ls, args);

	if(!(ch = grab_child_slot()))
		goto fail;
	if((pid = sysfork()) < 0)
		goto fail;
	
	if(pid == 0) {
		ret = execvpe(*args, args, envp);
		fail("exec", *args, ret);
	} else {
		ch->ifi = ls->ifi;
		ch->pid = pid;
		return;
	}
fail:
	child_exit(ls, 0, -1);
}

static void spawn_dhcp(struct link* ls, char* opts)
{
	char* args[] = { "dhcp", opts, ls->name, NULL };
	spawn(ls, args, environ);
}

/* link_* are callbacks for link status changes, called by the NL code */

void link_new(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
}

void link_wifi(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode2 == M2_KEEP)
		ls->mode2 = M2_SCAN;

	if(ls->mode2 == M2_SCAN)
		trigger_scan(ls);
}

void link_scan_done(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
}

void link_del(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
	
	stop_link_procs(ls->ifi, 0);
	deif_link_procs(ls->ifi);
}

void link_enabled(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(!(ls->state & S_WIRELESS))
		return;
	if(ls->mode2 != M2_SCAN)
		return;

	trigger_scan(ls);
}

void link_carrier(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode3 == M3_FIXED)
		; /* ... */
	else if(ls->mode3 == M3_LOCAL)
		spawn_dhcp(ls, "-g");
	else if(ls->mode3 == M3_DHCP)
		spawn_dhcp(ls, "-");
}

void link_disconnected(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
	terminate_link(ls);
}

void link_got_ip(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
}

void link_lost_ip(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->failed)
		terminate_link(ls);
	/* else we don't care */
}

/* conf_* are active commands to change link state, coming either
   from the user or from saved-configuration code */

int conf_down(struct link* ls)
{
	ls->mode2 = M2_DOWN;
	ls->mode3 = M3_DHCP;

	stop_link_procs(ls->ifi, 0);
	set_link_operstate(ls->ifi, IF_OPER_DOWN);

	return 0;
}

int conf_auto(struct link* ls)
{
	ls->mode2 = M2_SCAN;
	ls->mode3 = M3_DHCP;

	if((ls->state & (S_CARRIER | S_IPADDR)) == S_CARRIER)
		link_carrier(ls);
	else if(!(ls->state & S_ENABLED))
		set_link_operstate(ls->ifi, IF_OPER_UP);

	return 0;
}

static void spawn_wpa(struct link* ls)
{
	char* argv[] = { "wpa", wp.freq, wp.bssid, wp.ssid, wp.mode, NULL };
	char* envp[envcount+2];
	int i;

	envp[0] = wp.psk;
	for(i = 0; i < envcount; i++)
		envp[i+1] = environ[i];
	envp[i+1] = NULL;
	
	spawn(ls, argv, envp);
}

int conf_wpa(struct link* ls)
{
	if(!(ls->state & S_WIRELESS))
		return -EINVAL;
	if(ls->state & S_CARRIER)
		return -EBUSY;

	ls->mode2 = M2_SCAN;
	ls->mode3 = M3_DHCP;

	spawn_wpa(ls);

	return 0;
}
