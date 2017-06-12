#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/accept.h>
#include <sys/bind.h>
#include <sys/close.h>
#include <sys/kill.h>
#include <sys/listen.h>
#include <sys/recv.h>
#include <sys/socket.h>
#include <sys/write.h>
#include <sys/unlink.h>
#include <sys/setitimer.h>
#include <sys/brk.h>

#include <format.h>
#include <nlusctl.h>
#include <string.h>
#include <heap.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "common.h"
#include "wimon.h"

#define TIMEOUT 1

#define NOERROR 0
#define REPLIED 1
#define LATCHED 2

static void reply_long(struct conn* cn, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
}

static void reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	reply_long(cn, &uc);
}

static int estimate_scalist(void)
{
	int scansp = nscans*(sizeof(struct scan) + 10*sizeof(struct ucattr));

	return scansp + 128;
}

static int estimate_status(void)
{
	int scansp = nscans*(sizeof(struct scan) + 10*sizeof(struct ucattr));
	int linksp = nlinks*(sizeof(struct link) + 10*sizeof(struct ucattr));

	return scansp + linksp + 128;
}

static void prep_heap(struct heap* hp, int size)
{
	hp->brk = (void*)sysbrk(NULL);

	size += (PAGE - size % PAGE) % PAGE;

	hp->ptr = hp->brk;
	hp->end = (void*)sysbrk(hp->brk + size);
}

static void free_heap(struct heap* hp)
{
	sysbrk(hp->brk);
}

static void put_status_wifi(struct ucbuf* uc)
{
	struct ucattr* nn;
	struct link* ls;

	if(!(ls = find_link_slot(wifi.ifi)))
		return;

	nn = uc_put_nest(uc, ATTR_WIFI);
	uc_put_int(uc, ATTR_IFI, wifi.ifi);
	uc_put_str(uc, ATTR_NAME, ls->name);
	uc_put_int(uc, ATTR_MODE, wifi.mode);

	if(wifi.slen)
		uc_put_bin(uc, ATTR_SSID, wifi.ssid, wifi.slen);
	if(nonzero(wifi.bssid, sizeof(wifi.bssid)))
		uc_put_bin(uc, ATTR_BSSID, wifi.bssid, sizeof(wifi.bssid));
	if(wifi.freq)
		uc_put_int(uc, ATTR_FREQ, wifi.freq);

	uc_end_nest(uc, nn);
}

static int common_link_type(struct link* ls)
{
	if(ls->flags & S_NL80211)
		return LINK_NL80211;
	else
		return LINK_GENERIC;
}

static int common_link_state(struct link* ls)
{
	int state = ls->state;
	int flags = ls->flags;

	if(state == LS_ACTIVE)
		return LINK_ACTIVE;
	if(state == LS_STARTING)
		return LINK_STARTING;
	if(state == LS_STOPPING)
		return LINK_STOPPING;
	if(flags & S_CARRIER)
		return LINK_CARRIER;
	if(flags & S_ENABLED)
		return LINK_ENABLED;

	return LINK_OFF;
}

static void put_status_links(struct ucbuf* uc)
{
	struct link* ls;
	struct ucattr* nn;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi) continue;

		nn = uc_put_nest(uc, ATTR_LINK);
		uc_put_int(uc, ATTR_IFI,   ls->ifi);
		uc_put_str(uc, ATTR_NAME,  ls->name);
		uc_put_int(uc, ATTR_STATE, common_link_state(ls));
		uc_put_int(uc, ATTR_TYPE,  common_link_type(ls));

		if(ls->flags & S_IPADDR) {
			uc_put_bin(uc, ATTR_IPADDR, ls->ip, sizeof(ls->ip));
			uc_put_int(uc, ATTR_IPMASK, ls->mask);
		}

		uc_end_nest(uc, nn);
	}
}

static void put_status_scans(struct ucbuf* uc)
{
	struct scan* sc;
	struct ucattr* nn;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq) continue;
		nn = uc_put_nest(uc, ATTR_SCAN);
		uc_put_int(uc, ATTR_FREQ,   sc->freq);
		uc_put_int(uc, ATTR_TYPE,   sc->type);
		uc_put_int(uc, ATTR_SIGNAL, sc->signal);
		uc_put_int(uc, ATTR_PRIO,   sc->prio);
		uc_put_bin(uc, ATTR_BSSID, sc->bssid, sizeof(sc->bssid));
		uc_put_bin(uc, ATTR_SSID,  sc->ssid, sc->slen);
		uc_end_nest(uc, nn);
	}
}

static int cmd_status(struct conn* cn, struct ucmsg* msg)
{
	struct heap hp;
	struct ucbuf uc;

	prep_heap(&hp, estimate_status());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, CMD_STATUS);
	put_status_links(&uc);
	put_status_wifi(&uc);
	put_status_scans(&uc);
	uc_put_end(&uc);

	reply_long(cn, &uc);

	free_heap(&hp);

	return REPLIED;
}

static void reply_scanlist(struct conn* cn)
{
	struct heap hp;
	struct ucbuf uc;

	prep_heap(&hp, estimate_scalist());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, CMD_SCAN);
	put_status_scans(&uc);
	uc_put_end(&uc);

	reply_long(cn, &uc);

	free_heap(&hp);
}

static void release_latch(struct conn* cn, int err)
{
	struct itimerval old, new = {
		.interval = { 0, 0 },
		.value = { TIMEOUT, 0 }
	};

	syssetitimer(0, &new, &old);

	if(err)
		reply(cn, err);
	else if(cn->evt == SCAN)
		reply_scanlist(cn);
	else
		reply(cn, 0);

	syssetitimer(0, &old, NULL);

	cn->evt = 0;
	cn->ifi = 0;
}

void unlatch(int ifi, int evt, int err)
{
	struct conn* cn;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(!cn->fd || !cn->evt)
			continue;
		if(ifi != cn->ifi)
			continue;
		if(evt != cn->evt && evt != ANY)
			continue;

		release_latch(cn, err);
	}
}

static int setlatch(struct conn* cn, int ifi, int evt)
{
	if(cn->evt)
		return -EBUSY;

	cn->evt = evt;
	cn->ifi = ifi;

	return LATCHED;
}

static int get_ifi(struct ucmsg* msg)
{
	int* iv = uc_get_int(msg, ATTR_IFI);
	return iv ? *iv : 0;
}

static int switch_to_wifi(int rifi)
{
	int ifi;

	if((ifi = grab_wifi_device(rifi)) < 0)
		return ifi;

	stop_links_except(ifi);

	return 0;
}

static int find_wired_link(int rifi)
{
	int ifi = 0;
	struct link* ls;

	if(rifi && (ls = find_link_slot(rifi)))
		return rifi;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if(ls->flags & S_NL80211)
			continue;
		if(ifi)
			return -EMFILE;
		ifi = ls->ifi;
	}

	return ifi;
}

static int cmd_scan(struct conn* cn, struct ucmsg* msg)
{
	int ifi;

	if((ifi = grab_wifi_device(0)) < 0)
		return ifi;

	trigger_scan(ifi, 0);

	return setlatch(cn, WIFI, SCAN);
}

static int cmd_roaming(struct conn* cn, struct ucmsg* msg)
{
	int ret, rifi = get_ifi(msg);

	if((ret = switch_to_wifi(rifi)) < 0)
		return ret;
	if((ret = wifi_mode_roaming()))
		return ret;

	return setlatch(cn, WIFI, CONF);
}

static int cmd_fixedap(struct conn* cn, struct ucmsg* msg)
{
	int ret, rifi = get_ifi(msg);
	struct ucattr* ap;

	if(!(ap = uc_get(msg, ATTR_SSID)))
		return -EINVAL;

	uint8_t* ssid = uc_payload(ap);
	int slen = uc_paylen(ap);
	char* psk = uc_get_str(msg, ATTR_PSK);

	if((ret = switch_to_wifi(rifi)) < 0)
		return ret;
	if((ret = wifi_mode_fixedap(ssid, slen, psk)))
		return ret;

	return setlatch(cn, WIFI, CONF);
}

static int cmd_neutral(struct conn* cn, struct ucmsg* msg)
{
	wifi_mode_disabled();
	stop_links_except(0);

	return setlatch(cn, NONE, DOWN);
}

static int cmd_wired(struct conn* cn, struct ucmsg* msg)
{
	int ifi, ret;
	int rifi = get_ifi(msg);
	struct link* ls;

	wifi_mode_disabled();

	if((ifi = find_wired_link(rifi)) < 0)
		return ifi;
	if(!(ls = find_link_slot(ifi)))
		return -ENODEV;
	if((ret = start_wired_link(ls)) < 0)
		return ret;

	stop_links_except(ls->ifi);

	return setlatch(cn, ifi, CONF);
}

static const struct cmd {
	int cmd;
	int (*call)(struct conn* cn, struct ucmsg* msg);
} commands[] = {
	{ CMD_STATUS,  cmd_status  },
	{ CMD_NEUTRAL, cmd_neutral },
	{ CMD_ROAMING, cmd_roaming },
	{ CMD_FIXEDAP, cmd_fixedap },
	{ CMD_WIRED,   cmd_wired   },
	{ CMD_SCAN,    cmd_scan    },
	{ 0,           NULL        }
};

static void cancel_latch(struct conn* cn)
{
	if(!cn->evt)
		return;

	reply(cn, -EINTR);

	cn->evt = 0;
	cn->ifi = 0;
}

static void dispatch_cmd(struct conn* cn, struct ucmsg* msg)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	cancel_latch(cn);

	for(cd = commands; cd->cmd; cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		reply(cn, ret);
}

static void shutdown_conn(struct conn* cn)
{
	sysclose(cn->fd);
	memzero(cn, sizeof(*cn));
}

/* The code below allows for persistent connections, and can handle multiple
   commands per connection. This feature was crucial at some point, but that's
   no longer so. It is kept mostly as a sanity feature for foreign clients
   (anything that's not wictl), and also because it relaxes timing requirements
   for wictl connections. */

void handle_conn(struct conn* cn)
{
	int fd = cn->fd;
	char rbuf[1024];
	char* buf = rbuf;
	char* ptr = rbuf;
	char* end = rbuf + sizeof(rbuf);
	int flags = 0;
	int rb;

	struct ucmsg* msg;

	while((rb = sysrecv(fd, ptr, end - ptr, flags)) > 0) {
		ptr += rb;

		while((msg = uc_msg(buf, ptr - buf))) {
			dispatch_cmd(cn, msg);
			buf += msg->len;
		}

		if(buf >= ptr) {
			buf = rbuf;
			ptr = buf;
			flags = MSG_DONTWAIT;
		} else if(buf > rbuf) {
			memmove(rbuf, buf, ptr - buf);
			ptr = rbuf + (ptr - buf);
			buf = rbuf;
			flags = 0;
		} if(ptr >= end) {
			rb = -ENOBUFS;
			reply(cn, rb);
			break;
		}
	} if(rb < 0 && rb != -EAGAIN) {
		shutdown_conn(cn);
	}
}

void accept_ctrl(int sfd)
{
	int cfd;
	int gotcmd = 0;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn, reserve;
	struct itimerval itv = {
		.interval = { 0, 0 },
		.value = { TIMEOUT, 0 }
	};

	while((cfd = sysaccept(sfd, &addr, &addr_len)) > 0) {
		syssetitimer(0, &itv, NULL);
		gotcmd = 1;

		if(!(cn = grab_conn_slot()))
			memzero(cn = &reserve, sizeof(reserve));

		cn->fd = cfd;
		handle_conn(cn);

		if(cn == &reserve)
			shutdown_conn(&reserve);
	} if(gotcmd) {
		/* disable the timer in case it has been set */
		memzero(&itv, sizeof(itv));
		syssetitimer(0, &itv, NULL);
	}

	save_config();
}

void unlink_ctrl(void)
{
	sysunlink(WICTL);
}

void setup_ctrl(void)
{
	int fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = WICTL
	};

	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	if((fd = syssocket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	long ret;
	char* name = WICTL;

	ctrlfd = fd;

	if((ret = sysbind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", name, ret);
	else if((ret = syslisten(fd, 1)))
		fail("listen", name, ret);
	else
		return;

	ctrlfd = -1;
}
