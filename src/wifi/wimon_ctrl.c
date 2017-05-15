#include <bits/socket.h>
#include <bits/socket/unix.h>
#include <sys/accept.h>
#include <sys/alarm.h>
#include <sys/bind.h>
#include <sys/close.h>
#include <sys/getsockopt.h>
#include <sys/getuid.h>
#include <sys/kill.h>
#include <sys/listen.h>
#include <sys/read.h>
#include <sys/socket.h>
#include <sys/write.h>
#include <sys/unlink.h>
#include <sys/brk.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <heap.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "common.h"
#include "wimon.h"

#define TIMEOUT 1

struct latch latch;

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

static void unknown_command(int fd)
{
	reply_simple(fd, -ENOSYS);
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
		if(!sc->ifi) continue;

		nn = uc_put_nest(uc, ATTR_SCAN);
		uc_put_u32(uc, ATTR_IFI,    sc->ifi);
		uc_put_u32(uc, ATTR_FREQ,   sc->freq);
		uc_put_u32(uc, ATTR_AUTH,   sc->type);
		uc_put_u32(uc, ATTR_SIGNAL, sc->signal);
		uc_put_ubin(uc, ATTR_BSSID, sc->bssid, sizeof(sc->bssid));
		uc_put_ubin(uc, ATTR_SSID,  sc->ssid, sc->slen);
		uc_end_nest(uc, nn);
	}
}

static void cmd_status(int fd, struct ucmsg* msg)
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
}

/* Re-configuration is sequential task that has to wait for certain NL events.
   It's tempting to make it into something sequential as well, but that would
   require either implementing a mini-scheduler within wimon or spawning
   a second process to use the OS scheduler. Either option looks like a huge
   overkill for a task this simple.

   The possible sets of actions to perform are very limited. It's always
   [stop-wait]-[scan-wait]-[start-wait], with all three components optional.
   The incomding user commands are used to pre-configure the sequence in
   struct latch. Pieces of code strategically placed in respective event
   handles are then conditionally perform the requested actions and remove
   them from struct latch.

   The whole latch_release thing is to make the requests synchronous; wictl
   does not exit until the action has been performed. */

void latch_release(int code)
{
	int cfd = latch.cfd;

	latch.cfd = 0;
	latch.ifstop = 0;
	latch.ifscan = 0;
	latch.ifconf = 0;

	reply_simple(cfd, code);
}

void latch_proceed(void)
{
	struct link* ls;

	if(!latch.ifstop)
		goto scan;
	else if(!(ls = find_link_slot(latch.ifstop)))
		return latch_release(-ENODEV);
	else if(!(ls->flags & S_TERMRQ))
		return terminate_link(ls);
	else return;
scan:
	if(!latch.ifscan)
		goto conf;
	else if(!(ls = find_link_slot(latch.ifscan)))
		return latch_release(-ENODEV);
	else if(!ls->scan)
		return trigger_scan(ls, 0);
	else return;
conf:
	if(!latch.ifconf)
		goto done;
	else if(!(ls = find_link_slot(latch.ifstop)))
		return latch_release(-ENODEV);
	else if(!(ls->flags & S_IPADDR))
		return configure_link(ls);
done:
	return latch_release(0);
}

static int find_iface(int iflags)
{
	int ifi = 0;
	struct link* ls;
	int mask = S_WIRELESS;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if((ls->flags & mask) != iflags)
			continue;
		ifi = ifi ? -1 : ls->ifi;
	}

	if(ifi < 0)
		return -ESRCH;
	if(ifi == 0)
		return -ENODEV;

	return ifi;
}

static void cmd_wired(int fd, struct ucmsg* msg)
{
	int ifi;

	if((ifi = find_iface(0)) < 0)
		return reply_simple(fd, ifi);
	if(latch.cfd)
		return reply_simple(fd, -EBUSY);

	latch.cfd = fd;
	latch.ifconf = ifi;

	if(latch.uplink)
		latch.ifstop = latch.uplink;

	latch_proceed();
}

static void cmd_wless(int fd, struct ucmsg* msg)
{
	int ifi;

	if((ifi = find_iface(S_WIRELESS)) < 0)
		return reply_simple(fd, ifi);
	if(latch.cfd)
		return reply_simple(fd, -EBUSY);

	latch.cfd = fd;
	latch.ifconf = ifi;
	if(latch.uplink)
		latch.ifstop = latch.uplink;
	latch.uplink = ifi;

	latch_proceed();
}

static void cmd_stop(int fd, struct ucmsg* msg)
{
	if(latch.cfd)
		return reply_simple(fd, -EBUSY);
	if(!latch.uplink)
		return reply_simple(fd, -ECHILD);

	latch.cfd = fd;
	latch.ifstop = latch.uplink;
	latch.uplink = 0;

	reply_simple(fd, -EALREADY);
}

static void cmd_scan(int fd, struct ucmsg* msg)
{
	if(latch.cfd)
		return reply_simple(fd, -EBUSY);

	latch.cfd = fd;
	latch.ifscan = -1;

	scan_all_wifis();
}

static void cmd_reconn(int fd, struct ucmsg* msg)
{
	reply_simple(fd, -ENODEV);
}

static void cmd_cancel(int fd, struct ucmsg* msg)
{
	if(!latch.cfd) {
		reply_simple(fd, -ECHILD);
	} else {
		latch_release(-EINTR);
		reply_simple(fd, 0);
	}
}

static void parse_cmd(int fd, struct ucmsg* msg)
{
	switch(msg->cmd) {
		case CMD_STATUS: return cmd_status(fd, msg);
		case CMD_WIRED: return cmd_wired(fd, msg);
		case CMD_WLESS: return cmd_wless(fd, msg);
		case CMD_STOP: return cmd_stop(fd, msg);
		case CMD_SCAN: return cmd_scan(fd, msg);
		case CMD_RECONN: return cmd_reconn(fd, msg);
		case CMD_CANCEL: return cmd_cancel(fd, msg);
		default: return unknown_command(fd);
	}
}

static void read_cmd(int fd)
{
	int rb;
	char rbuf[64+4];
	struct ucmsg* msg;

	if((rb = sysread(fd, rbuf, sizeof(rbuf))) < 0)
		return warn("recvmsg", NULL, rb);
	if(!rb)
		return;
	if(rb > sizeof(rbuf)-4)
		return warn("recvmsg", "message too long", 0);
	if(!(msg = uc_msg(rbuf, rb)))
		return warn("recvmsg", "message malformed", 0);
	if(rb > msg->len)
		return warn("recvmsg", "trailing bytes", 0);

	parse_cmd(fd, msg);
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
		sysclose(cfd);
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
