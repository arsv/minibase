#include <bits/errno.h>

#include <null.h>
#include <format.h>
#include <string.h>

#include "wimon.h"

struct uplink uplink;

void link_new(struct link* ls)
{
	int ifi = ls->ifi;

	eprintf("%s %s\n", __FUNCTION__, ls->name);

	load_link(ls);

	if(ls->mode == LM_NOT)
		return;
	if(ls->mode == LM_OFF) {
		if(ls->flags & S_IPADDR)
			del_link_addresses(ifi);
	} else {
		if(ls->flags & S_CARRIER)
			link_carrier(ls);
		else if(ls->flags & S_ENABLED)
			link_enabled(ls);
	}
}

void link_enabled(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode == LM_NOT || ls->mode == LM_OFF)
		return;
	if(ls->flags & S_NL80211)
		wifi_ready(ls);
	else /* The link came up but there's no carrier */
		unlatch(ls->ifi, CONF, -ENETDOWN);
}

void link_carrier(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode == LM_NOT || ls->mode == LM_OFF)
		return;

	if(ls->mode == LM_STATIC)
		; /* XXX */
	else if(ls->mode == LM_LOCAL)
		spawn_dhcp(ls, "-g");
	else if(ls->mode == LM_DHCP)
		spawn_dhcp(ls, NULL);
}

void link_ipaddr(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	unlatch(ls->ifi, CONF, 0);

	if(ls->flags & S_NL80211)
		wifi_connected(ls);
}

static int any_stopping_links(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(ls->ifi && (ls->flags & S_STOPPING))
			return 1;

	return 0;
}

/* Whenever a link goes down, for any reason, all its remaining procs must
   be stopped and its ip configuration must be flushed. Not doing this means
   the link may be left in mid-way state, confusing the usespace. */

static void wait_link_down(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->flags & S_CHILDREN) {
		eprintf("  children\n");
		return stop_link_procs(ls, 0);
	} if(ls->flags & S_IPADDR) {
		eprintf("  ipaddrs\n");
		return del_link_addresses(ls->ifi);
	} if(ls->flags & S_APLOCK) {
		eprintf("  aplock\n");
		return trigger_disconnect(ls->ifi);
	}

	eprintf("stopped %s\n", ls->name);

	if(ls->flags & S_NL80211)
		wifi_conn_fail(ls);

	ls->flags &= ~S_STOPPING;

	unlatch(ls->ifi, DOWN, 0);
	unlatch(ls->ifi, CONF, -ENETDOWN);

	if(!any_stopping_links())
		unlatch(NONE, DOWN, 0);
}

void terminate_link(struct link* ls)
{
	eprintf("terminating %s\n", ls->name);
	ls->flags &= ~S_UPCOMING;
	ls->flags |= S_STOPPING;

	wait_link_down(ls);
}

/* Outside of link-stopping procedure, the only remotely sane case
   for this to happen is expiration of DHCP-set address. If so, try
   to re-run dhcp. And that's about it really. */

void link_ipgone(struct link* ls)
{
	if(ls->flags & S_STOPPING)
		return wait_link_down(ls);
	if(ls->mode == LM_NOT || ls->mode == LM_OFF)
		return;
	if(ls->flags & S_CARRIER)
		link_carrier(ls);
}

void link_apgone(struct link* ls)
{
	if(ls->flags & S_STOPPING)
		return wait_link_down(ls);
}

void link_down(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(!(ls->flags & S_STOPPING))
		terminate_link(ls);
}

void link_child_exit(struct link* ls, int status)
{
	eprintf("%s %s %i\n", __FUNCTION__, ls->name, status);

	if(ls->flags & S_STOPPING)
		wait_link_down(ls);
	else if(status)
		terminate_link(ls);
	/* else it's ok */
}

void link_gone(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	stop_link_procs(ls, 1);

	unlatch(ls->ifi, DOWN, 0); /* or ENODEV? */
	unlatch(ls->ifi, CONF, -ENODEV);
	unlatch(ls->ifi, SCAN, -ENODEV);

	if(ls->flags & S_NL80211)
		wifi_gone(ls);
}

/* When wimon exits, remove addresses from managed links.
   This is primarily meant for wireless links but makes some sense
   for ethernet ports as well. */

void finalize_links(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi || ls->mode == LM_NOT)
			continue;
		if(ls->flags & S_IPADDR)
			del_link_addresses(ls->ifi);
		/* del_link_routes */
	}
}

int stop_all_links(void)
{
	struct link* ls;
	int down = 0;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi || ls->mode == LM_NOT)
			continue;
		if(!(ls->flags & (S_CHILDREN | S_IPADDR)))
			continue;

		terminate_link(ls);

		if(ls->flags & S_STOPPING)
			down++;
	}

	return down;
}

int switch_uplink(int ifi)
{
	struct link* ls;
	struct link* rls = NULL;

	for(ls = links; ls < links + nlinks; ls++)
		if(!ls->ifi)
			continue;
		else if(ls->ifi == ifi)
			rls = ls;
		else if(ls->flags & (S_UPLINK | S_UPCOMING))
			return -EBUSY;

	if(!(ls = rls))
		return -ENODEV;

	if(ls->flags & (S_UPLINK | S_UPCOMING))
		return 0;

	ls->mode = LM_DHCP;
	ls->flags |= S_UPCOMING;

	return 0;
}
