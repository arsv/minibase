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

	if(ls->mode & LM_NOT)
		return;
	if(ls->mode & LM_OFF) {
		if(ls->flags & S_IPADDR)
			del_link_addresses(ifi);
		if(ls->flags & S_ENABLED)
			set_link_operstate(ifi, IF_OPER_DOWN);
	} else {
		if(!(ls->flags & S_ENABLED))
			set_link_operstate(ifi, IF_OPER_UP);
		else
			link_enabled(ls);
	}
}

/* Unlike other link_* calls that come from RTNL, this one gets triggered
   by GENL code. The reason for handling it here is to make sure both RTNL
   and GENL parts of the link are in place by the time wifi_* code sees
   the link. Since those are distinct fds, GENL message may arrive before
   the corresponding RTNL one.

   RTNL crucially carries the link name which is used to load link settings
   including LM_NOT | LM_OFF. */

void link_nl80211(struct link* ls)
{
	if(!(ls->flags & S_NETDEV))
		return;
	if(ls->mode & (LM_NOT | LM_OFF))
		return;
	if(ls->flags & S_ENABLED)
		wifi_ready(ls);
}

void link_enabled(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode & (LM_NOT | LM_OFF))
		return;
	if(ls->flags & S_NL80211)
		wifi_ready(ls);
	else /* The link came up but there's no carrier */
		unlatch(ls->ifi, CONF, -ENETDOWN);
}

void link_carrier(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode & (LM_NOT | LM_OFF))
		return;

	ls->flags |= S_MANAGED;

	if(ls->mode & LM_STATIC)
		; /* XXX */
	else if(ls->mode & LM_LOCAL)
		spawn_dhcp(ls, "-g");
	else
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
		if(!ls->ifi || (ls->mode & LM_NOT))
			continue;
		else if(ls->flags & S_STOPPING) {
			eprintf("waiting for %s\n", ls->name);
			return 1;
		}

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
	}

	eprintf("stopped %s\n", ls->name);

	if(ls->flags & S_NL80211)
		wifi_conn_fail(ls);

	ls->flags &= ~(S_STOPPING | S_MANAGED);

	unlatch(ls->ifi, DOWN, 0);
	unlatch(ls->ifi, CONF, -ENETDOWN);

	if(!any_stopping_links())
		unlatch(NONE, DOWN, 0);
}

void terminate_link(struct link* ls)
{
	eprintf("terminating %s\n", ls->name);
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
	if(!(ls->flags & S_MANAGED))
		return;
	if(ls->flags & S_CARRIER)
		link_carrier(ls);
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
		if(!ls->ifi || (ls->mode & LM_NOT))
			continue;
		if(ls->flags & S_IPADDR)
			del_link_addresses(ls->ifi);
		if(ls->flags & S_ENABLED)
			set_link_operstate(ls->ifi, IF_OPER_DOWN);
		/* del_link_routes */
	}
}

int stop_all_links(void)
{
	struct link* ls;
	int down = 0;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi || (ls->mode & LM_NOT))
			continue;

		if(!(ls->flags & (S_CHILDREN | S_IPADDR)))
			continue;

		terminate_link(ls);

		if(ls->flags & S_STOPPING)
			down++;
	}

	return down;
}
