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

static void abnormal_exit(struct link* ls, int pid)
{
	eprintf("abnormal exit on link %s\n", ls->name);

	if(ls->failed)
		return;

	ls->failed = 1;

	stop_link_procs(ls->ifi, pid);

	if(!(ls->state & S_IPADDR))
		return;

	flush_link_address(ls->ifi);
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
		if(!(ls = find_link_slot(ch->ifi)))
			goto next;
		if(status)
			abnormal_exit(ls, ch->pid);
	next:
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
	abnormal_exit(ls, 0);
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

void link_scan(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
}

void link_del(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
	
	stop_link_procs(ls->ifi, 0);
	deif_link_procs(ls->ifi);
}

static void spawn_dhcp(struct link* ls)
{
	char* args[] = { "dhcp", ls->name, NULL };
	spawn(ls, args, environ);
}

static void spawn_dhcp_local(struct link* ls)
{
	char* args[] = { "dhcp", "-g", ls->name, NULL };
	spawn(ls, args, environ);
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
		spawn_dhcp_local(ls);
	else if(ls->mode3 == M3_DHCP)
		spawn_dhcp(ls);
}

void link_down(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	stop_link_procs(ls->ifi, 0);
	flush_link_address(ls->ifi);
}

void link_addr(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
}

/* conf_* are active commands to change link state, coming either
   from the user or from saved-configuration code */

int conf_down(struct link* ls)
{
	ls->mode2 = M2_DOWN;
	ls->mode3 = M3_DHCP;

	stop_link_procs(ls->ifi, 0);

	if(ls->state & S_WIRELESS)
		set_link_operstate(ls->ifi, OPERSTATE_DOWN);

	return 0;
}

int conf_auto(struct link* ls)
{
	ls->mode2 = M2_SCAN;
	ls->mode3 = M3_DHCP;

	if((ls->state & (S_CARRIER | S_IPADDR)) == S_CARRIER)
		link_carrier(ls);
	else if(!(ls->state & S_ENABLED))
		set_link_operstate(ls->ifi, OPERSTATE_UP);

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
