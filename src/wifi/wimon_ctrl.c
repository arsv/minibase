#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/accept.h>
#include <sys/alarm.h>
#include <sys/bind.h>
#include <sys/close.h>
#include <sys/kill.h>
#include <sys/listen.h>
#include <sys/read.h>
#include <sys/socket.h>
#include <sys/write.h>
#include <sys/unlink.h>
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

struct {
	int evt;
	int ifi;
	int cfd;
} latch;

int uplock;

static void reply(int fd, struct ucbuf* uc)
{
	writeall(fd, uc->brk, uc->ptr - uc->brk);
}

static void reply_simple(int fd, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	reply(fd, &uc);
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

static void put_status_links(struct ucbuf* uc)
{
	struct link* lk;
	struct ucattr* nn;

	for(lk = links; lk < links + nlinks; lk++) {
		if(!lk->ifi) continue;

		nn = uc_put_nest(uc, ATTR_LINK);
		uc_put_u32(uc, ATTR_IFI,    lk->ifi);
		uc_put_str(uc, ATTR_NAME,   lk->name);
		uc_put_u32(uc, ATTR_FLAGS,  lk->flags);
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
		uc_put_u32(uc, ATTR_FREQ,   sc->freq);
		uc_put_u32(uc, ATTR_AUTH,   sc->type);
		uc_put_u32(uc, ATTR_SIGNAL, sc->signal);
		uc_put_ubin(uc, ATTR_BSSID, sc->bssid, sizeof(sc->bssid));
		uc_put_ubin(uc, ATTR_SSID,  sc->ssid, sc->slen);
		uc_end_nest(uc, nn);
	}
}

static int cmd_status(int fd, struct ucmsg* msg)
{
	struct heap hp;
	struct ucbuf uc;

	prep_heap(&hp, estimate_status());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, CMD_STATUS);
	put_status_links(&uc);
	put_status_scans(&uc);
	uc_put_end(&uc);

	reply(fd, &uc);

	free_heap(&hp);

	return REPLIED;
}

static void reply_scanlist(int fd, int ifi)
{
	struct heap hp;
	struct ucbuf uc;

	prep_heap(&hp, estimate_scalist());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, CMD_SCAN);
	put_status_scans(&uc);
	uc_put_end(&uc);

	reply(fd, &uc);

	free_heap(&hp);
}

/* Re-configuration is sequential task that has to wait for certain NL events.
   It's tempting to make it into something sequential as well, but that would
   require either implementing a mini-scheduler within wimon or spawning
   a second process to use the OS scheduler. Either option looks like a huge
   overkill for a task this simple.

   So instead, most of sequentiality is moved over to wictl which is sequential
   anyway, and wimon only gets a single "latch" triggering on certain events. */

static void latch_reset(void)
{
	sysclose(latch.cfd);

	latch.cfd = 0;
	latch.evt = 0;
	latch.ifi = 0;
}

void unlatch(int ifi, int evt, int err)
{
	int cfd = latch.cfd;

	if(!latch.cfd)
		return;
	if(ifi != latch.ifi)
		return;
	if(evt != latch.evt)
		return;

	sysalarm(TIMEOUT);

	if(err)
		reply_simple(cfd, err);
	else if(evt == SCAN)
		reply_scanlist(cfd, latch.ifi);
	else
		reply_simple(cfd, 0);

	sysalarm(0);

	latch_reset();
}

static int setlatch(int ifi, int evt, int cfd)
{
	if(latch.cfd)
		return -EBUSY;

	latch.cfd = cfd;
	latch.evt = evt;
	latch.ifi = ifi;

	return LATCHED;
}

static int cmd_cancel(int fd, struct ucmsg* msg)
{
	if(latch.cfd)
		sysclose(latch.cfd);

	latch.cfd = 0;
	latch.evt = 0;
	latch.ifi = 0;

	return NOERROR;
}

static int get_ifi(struct ucmsg* msg)
{
	uint32_t* u32 = uc_get_u32(msg, ATTR_IFI);
	return u32 ? *u32 : 0;
}

static int cmd_scan(int fd, struct ucmsg* msg)
{
	int ifi;

	if((ifi = grab_wifi_device(0)) < 0)
		return ifi;
	if(latch.cfd)
		return -EAGAIN;

	trigger_scan(ifi, 0);

	return setlatch(WIFI, SCAN, fd);
}

static int cmd_roaming(int fd, struct ucmsg* msg)
{
	int ifi, ret;
	int rifi = get_ifi(msg);

	eprintf("%s\n", __FUNCTION__);

	if((ifi = grab_wifi_device(rifi)) < 0)
		return ifi;
	if((ret = switch_uplink(ifi))) {
		eprintf("%s switch_uplink %i\n", __FUNCTION__, ret);
		return ret;
	} if((ret = wifi_mode_roaming())) {
		eprintf("%s wifi_mode_roaming %i\n", __FUNCTION__, ret);
		return ret;
	}

	return NOERROR;
}

static int cmd_fixedap(int fd, struct ucmsg* msg)
{
	int ifi, ret;
	struct ucattr* ap;
	int rifi = get_ifi(msg);

	eprintf("%s\n", __FUNCTION__);

	if(!(ap = uc_get(msg, ATTR_SSID)))
		return -EINVAL;
	if(latch.cfd)
		return -EAGAIN;

	int slen = ap->len - sizeof(*ap);
	uint8_t* ssid = (uint8_t*)ap->payload;

	if((ifi = grab_wifi_device(rifi)) < 0)
		return ifi;
	if((ret = switch_uplink(ifi)))
		return ret;
	if((ret = wifi_mode_fixedap(ssid, slen)))
		return ret;

	return NOERROR;
}

static int cmd_neutral(int fd, struct ucmsg* msg)
{
	int ret;

	eprintf("%s\n", __FUNCTION__);

	if(latch.cfd)
		return -EAGAIN;

	wifi_mode_disabled();

	if((ret = stop_all_links()) <= 0)
		return ret;

	return setlatch(NONE, DOWN, fd);
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
		if(ls->mode & LM_NOT)
			continue;
		if(ls->flags & S_NL80211)
			continue;
		if(ifi)
			return -EMFILE;
		ifi = ls->ifi;
	}

	return ifi;
}

static int cmd_wired(int fd, struct ucmsg* msg)
{
	int ret, ifi;
	int rifi = get_ifi(msg);
	struct link* ls;

	eprintf("%s\n", __FUNCTION__);

	if((ifi = find_wired_link(rifi)) < 0)
		return ifi;
	if(latch.cfd)
		return -EAGAIN;
	if(!(ls = find_link_slot(ifi)))
		return -ENODEV;
	if((ret = switch_uplink(ifi)) < 0)
		return ret;

	if(ls->flags & S_CARRIER)
		link_carrier(ls);
	else
		return -ENETDOWN;

	ls->flags |= S_UPCOMING;

	return setlatch(ifi, CONF, fd);
}

static const struct cmd {
	int cmd;
	int (*call)(int fd, struct ucmsg* msg);
} commands[] = {
	{ CMD_STATUS,  cmd_status  },
	{ CMD_NEUTRAL, cmd_neutral },
	{ CMD_ROAMING, cmd_roaming },
	{ CMD_FIXEDAP, cmd_fixedap },
	{ CMD_WIRED,   cmd_wired   },
	{ CMD_SCAN,    cmd_scan    },
	{ CMD_CANCEL,  cmd_cancel  },
	{ 0,           NULL        }
};

static int dispatch_cmd(int fd, struct ucmsg* msg)
{
	const struct cmd* cd;
	int cmd = msg->cmd;

	for(cd = commands; cd->cmd; cd++)
		if(cd->cmd == cmd)
			return cd->call(fd, msg);

	return -ENOSYS;
}

static void read_cmd(int fd)
{
	int rb;
	char rbuf[64+4];
	struct ucmsg* msg;
	int err;

	/* XXX: multiple messages here? */
	if((rb = sysread(fd, rbuf, sizeof(rbuf))) <= 0)
		return warn("recvmsg", rb ? NULL : "empty message", rb);

	if(rb > sizeof(rbuf)-4)
		err = -E2BIG;
	else if(!(msg = uc_msg(rbuf, rb)))
		err = -EBADMSG;
	else
		err = dispatch_cmd(fd, msg);

	if(err == LATCHED)
		return;
	if(err != REPLIED)
		reply_simple(fd, err);

	sysclose(fd);
}

void accept_ctrl(int sfd)
{
	int cfd;
	int gotcmd = 0;
	struct sockaddr addr;
	int addr_len = sizeof(addr);

	while((cfd = sysaccept(sfd, &addr, &addr_len)) > 0) {
		gotcmd = 1;
		sysalarm(TIMEOUT);
		read_cmd(cfd);
	} if(gotcmd) {
		/* disable the timer in case it has been set */
		sysalarm(0);
	}
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
