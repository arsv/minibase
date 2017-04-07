#include <sys/waitpid.h>
#include <sys/execve.h>
#include <sys/fork.h>
#include <sys/kill.h>
#include <sys/_exit.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <null.h>
#include <fail.h>

#include "wimon.h"
#include "wimon_proc.h"
#include "wimon_slot.h"

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

void drop_link_procs(struct link* ls)
{
	struct child* ch;

	stop_link_procs(ls->ifi, 0);

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->ifi == ls->ifi)
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

void terminate_link(struct link* ls)
{
	int ifi = ls->ifi;
	int procs = any_link_procs(ifi);
	int firstcall = (ls->mode & LM_TERMRQ);

	if(firstcall)
		ls->mode |= LM_TERMRQ;
	if(procs && firstcall)
		return stop_link_procs(ifi, 0);
	else if(procs)
		return; /* wait until everything terminates via child_exit */
	if(ls->flags & S_IPADDR)
		return del_link_addresses(ifi); /* and wait for link_deconfed */

	ls->flags &= ~LM_TERMRQ;

	link_terminated(ls);
}

static void child_exit(struct link* ls, int pid, int abnormal)
{
	if(abnormal)
		eprintf("abnormal exit on link %s\n", ls->name);
	else
		eprintf("normal exit on link %s\n", ls->name);

	if(abnormal || ls->mode & LM_TERMRQ)
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

static void spawn(struct link* ls, char** args, char** envp)
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

void spawn_dhcp(struct link* ls, char* opts)
{
	char* args[5];
	char** p = args;
	
	*p++ = "dhcp";
	if(opts) *p++ = opts;
	*p++ = ls->name;
	*p++ = NULL;

	spawn(ls, args, environ);
}

static void prep_wpa_bssid(char* buf, int len, uint8_t* bssid)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtmac(p, e, bssid);
	*p++ = '\0';
}

static void prep_wpa_ssid(char* buf, int len, uint8_t* ssid, int slen)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtraw(p, e, ssid, slen);
	*p++ = '\0';
}

static void prep_wpa_freq(char* buf, int len, int freq)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtint(p, e, freq);
	*p++ = '\0';
}

static void prep_wpa_psk(char* buf, int len, char* psk)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, "PSK=");
	p = fmtstr(p, e, psk);
	*p++ = '\0';
}

static void prep_wpa_env(char** envp, char* pskvar, int envc)
{
	envp[0] = pskvar;
	memcpy(envp + 1, environ, (envc+1)*sizeof(char*));
}

void spawn_wpa(struct link* ls, struct scan* sc, char* mode, char* psk)
{
	char bssid[6*3];
	char freq[10];
	char ssid[SSIDLEN+2];

	prep_wpa_ssid(ssid, sizeof(ssid), sc->ssid, sc->slen);
	prep_wpa_bssid(bssid, sizeof(bssid), sc->bssid);
	prep_wpa_freq(freq, sizeof(freq), sc->freq);

	char* argv[] = { "wpa", freq, bssid, ssid, mode, NULL };

	char pskvar[10+strlen(psk)];
	char* envp[envcount+2];

	prep_wpa_psk(pskvar, sizeof(pskvar), psk);
	prep_wpa_env(envp, pskvar, envcount);

	spawn(ls, argv, envp);
	
	memzero(pskvar, sizeof(pskvar));
}
