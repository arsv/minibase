#include <sys/waitpid.h>
#include <sys/execve.h>
#include <sys/fork.h>
#include <sys/kill.h>
#include <sys/alarm.h>
#include <sys/pause.h>
#include <sys/_exit.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <null.h>
#include <fail.h>

#include "wimon.h"

/* This part deals with child processes. There are only two kinds
   of those, dhcp and wpa, but the code just happens to be quite generic.

   Normal (status 0) exits are always allowed, and indicate that the tool
   has done its job. Any abnormal exit is assumed to be non-recoverable,
   at least at this level; the link gets terminated, and it's up to the
   management code to re-try if necessary. */

static void dump_spawn(struct link* ls, char** args)
{
	char** p;

	eprintf("spawn %s :", ls->name);
	for(p = args; *p; p++)
		eprintf(" %s", *p);
	eprintf("\n");
}

void stop_link_procs(struct link* ls, int drop)
{
	struct child* ch;
	int ifi = ls->ifi;

	if(ls->flags & S_SIGSENT)
		return;

	ls->flags |= S_SIGSENT;

	for(ch = children; ch < children + nchildren; ch++) {
		if(ch->ifi != ifi)
			continue;
		if(ch->pid <= 0)
			continue;

		syskill(ch->pid, SIGTERM);

		if(!drop) continue;

		ch->ifi = 0;
	}
}

int any_pids_left(void)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->pid > 0)
			return 1;

	return 0;
}

void stop_all_procs(void)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->pid > 0)
			syskill(ch->pid, SIGTERM);
}

static int any_link_procs(int ifi)
{
	struct child* ch;

	for(ch = children; ch < children + nchildren; ch++)
		if(ch->ifi == ifi)
			return 1;

	return 0;
}

void waitpids(void)
{
	struct child* ch;
	struct link* ls;
	int pid;
	int status;
	int ifi;

	while((pid = syswaitpid(-1, &status, WNOHANG)) > 0) {
		if(!(ch = find_child_slot(pid)))
			continue;
		if((ifi = ch->ifi) < 0)
			continue;

		free_child_slot(ch);

		if(!(ls = find_link_slot(ifi)))
			continue;
		if(!any_link_procs(ifi))
			ls->flags &= ~(S_CHILDREN | S_SIGSENT);

		link_child_exit(ls, status);
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
		ls->flags |= S_CHILDREN;
		return;
	}
fail:
	link_child_exit(ls, -1);
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

/* Stuff lik ssid, bssid, freq and psk should have been parameters here,
   but those always come from the same location (struct wifi) and make
   the prototype really messy, so we take a shortcut here. Also helps
   in that the caller does not need to care about struct wifi internals.

   Both link and mode may be variable here. It would really make sense
   to derive them here from parts of struct wifi, but that requires some
   error handling *and* the caller has it all anyway. */

void spawn_wpa(struct link* ls, char* mode)
{
	char bssid[6*3];
	char freq[10];
	char ssid[SSIDLEN+2];
	char* dev = ls->name;

	prep_wpa_ssid(ssid, sizeof(ssid), wifi.ssid, wifi.slen);
	prep_wpa_bssid(bssid, sizeof(bssid), wifi.bssid);
	prep_wpa_freq(freq, sizeof(freq), wifi.freq);

	char* argv[] = { "wpa", dev, freq, bssid, ssid, mode, NULL };

	char pskvar[10+strlen(wifi.psk)];
	char* envp[envcount+2];

	prep_wpa_psk(pskvar, sizeof(pskvar), wifi.psk);
	prep_wpa_env(envp, pskvar, envcount);

	spawn(ls, argv, envp);

	memzero(pskvar, sizeof(pskvar));
}
