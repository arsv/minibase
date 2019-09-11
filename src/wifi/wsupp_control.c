#include <bits/socket/packet.h>
#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/ppoll.h>

#include <nlusctl.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "wsupp.h"

/* Userspace control socket code, accepts and handles commands
   from the frontend tool, `wifi`. */

int ctrlfd;

static char rxbuf[100];
static char txbuf[100];

#define CN struct conn* cn __unused
#define MSG struct ucmsg* msg __unused

#define REPLIED 1

static void drop_connection(CN)
{
	sys_shutdown(cn->fd, SHUT_RDWR);
}

static void send_report(struct ucbuf* uc)
{
	struct conn* cn;
	int fd;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(!cn->rep || (fd = cn->fd) <= 0)
			continue;

		if(uc_send_timed(cn->fd, uc) >= 0)
			continue;

		drop_connection(cn);
	}
}

static void report_simple(int cmd)
{
	char buf[64];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);
	uc_put_end(&uc);

	send_report(&uc);
}

static void report_station(int cmd)
{
	struct ucbuf uc;
	char buf[256];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);
	uc_put_bin(&uc, ATTR_SSID, ap.ssid, ap.slen);
	uc_put_bin(&uc, ATTR_BSSID, ap.bssid, sizeof(ap.bssid));
	uc_put_int(&uc, ATTR_FREQ, ap.freq);
	uc_put_end(&uc);

	send_report(&uc);
}

void report_scan_end(int err)
{
	char buf[64];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, REP_WI_SCAN_END);
	if(err) uc_put_int(&uc, ATTR_ERROR, err);
	uc_put_end(&uc);

	send_report(&uc);
}

void report_connecting(void)
{
	report_station(REP_WI_CONNECTING);
}

void report_disconnect(void)
{
	report_station(REP_WI_DISCONNECT);
}

void report_no_connect(void)
{
	report_simple(REP_WI_NO_CONNECT);
}

void report_link_ready(void)
{
	report_station(REP_WI_LINK_READY);
}

static int send_reply(CN, struct ucbuf* uc)
{
	if(uc_send_timed(cn->fd, uc) < 0)
		drop_connection(cn);

	return REPLIED;
}

static int reply(CN, int err)
{
	struct ucbuf uc;

	uc_buf_set(&uc, txbuf, sizeof(txbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	return send_reply(cn, &uc);
}

static uint estimate_status(void)
{
	uint scansp = nscans*(sizeof(struct scan) + 10*sizeof(struct ucattr));

	scansp += (hp.ptr - hp.org); /* IEs, total size */

	return scansp + 128;
}

static void put_status_wifi(struct ucbuf* uc)
{
	int tm;

	if(ifindex <= 0)
		return;

	uc_put_int(uc, ATTR_IFI, ifindex);
	uc_put_str(uc, ATTR_NAME, ifname);
	uc_put_int(uc, ATTR_STATE, operstate);

	if(ap.slen)
		uc_put_bin(uc, ATTR_SSID, ap.ssid, ap.slen);
	if((tm = time_to_scan()) >= 0)
		uc_put_int(uc, ATTR_TIME, tm);

	if(!ap.freq)
		return; /* not connected to particular AP */

	uc_put_bin(uc, ATTR_BSSID, ap.bssid, sizeof(ap.bssid));
	uc_put_int(uc, ATTR_FREQ, ap.freq);
}

static void put_status_scans(struct ucbuf* uc)
{
	struct scan* sc;
	struct ucattr* nn;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;

		nn = uc_put_nest(uc, ATTR_SCAN);

		uc_put_int(uc, ATTR_FREQ,   sc->freq);
		uc_put_int(uc, ATTR_SIGNAL, sc->signal);
		uc_put_bin(uc, ATTR_BSSID,  sc->bssid, sizeof(sc->bssid));

		if(sc->ies)
			uc_put_bin(uc, ATTR_IES,  sc->ies, sc->ieslen);

		uc_end_nest(uc, nn);
	}
}

static int cmd_status(CN, MSG)
{
	struct ucbuf uc;
	int ret;

	if(extend_heap(estimate_status()) < 0)
		return -ENOMEM;

	uc_buf_set(&uc, hp.ptr, hp.brk - hp.ptr);
	uc_put_hdr(&uc, 0);
	put_status_wifi(&uc);
	put_status_scans(&uc);
	uc_put_end(&uc);

	ret = send_reply(cn, &uc);

	maybe_trim_heap();

	return ret;
}

static int cmd_detach(CN, MSG)
{
	int ret;

	if(ifindex <= 0)
		return -EALREADY;
	if((ret = ap_detach()) < 0)
		return ret;

	close_netlink();

	return 0;
}

static int cmd_setdev(CN, MSG)
{
	int* ifi;
	int ret;

	if(ifindex > 0)
		return -EBUSY;
	if(!(ifi = uc_get_int(msg, ATTR_IFI)))
		return -EINVAL;

	if((ret = open_netlink(*ifi)) < 0)
		return ret;
	if((ret = ap_resume()) < 0)
		return ret;

	cn->rep = 1;

	return 0;
}

static int cmd_scan(CN, MSG)
{
	int ret;

	if(ifindex <= 0)
		return -ENODEV;
	if((ret = start_scan(0)) < 0)
		return ret;

	cn->rep = 1;

	return 0;
}

static int cmd_neutral(CN, MSG)
{
	int ret;

	if((ret = ap_disconnect()) < 0)
		return ret;

	cn->rep = 1;

	return 0;
}

static int cmd_resume(CN, MSG)
{
	if(ifindex <= 0)
		return -ENODEV;

	cn->rep = 1;

	return ap_resume();
}

/* ACK to the command should preceed any notifications caused by the command.
   Since reassess_wifi_situation() is a pretty long and involved piece of code,
   we reply early to make sure possible messages generated while starting
   connection do not confuse the client.

   Note at this point reassess_wifi_situation() does *not* generate any
   notifications. Not even SCANNING, that one is issued reactively on
   NL80211_CMD_TRIGGER_SCAN. */

static int cmd_connect(CN, MSG)
{
	struct ucattr* assid;
	struct ucattr* apsk;
	int ret;

	if(ifindex <= 0)
		return -ENODEV;
	if(!(assid = uc_get(msg, ATTR_SSID)))
		return -EINVAL;
	if(!(apsk = uc_get(msg, ATTR_PSK)))
		return -EINVAL;

	byte* ssid = uc_payload(assid);
	int slen = uc_paylen(assid);

	if(uc_paylen(apsk) != 32)
		return -EINVAL;
	if((ret = ap_connect(ssid, slen, uc_payload(apsk))) < 0)
		return ret;

	cn->rep = 1; /* for connect/no-connect notifications */

	return 0;
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_WI_STATUS,  cmd_status  },
	{ CMD_WI_SETDEV,  cmd_setdev  },
	{ CMD_WI_SCAN,    cmd_scan    },
	{ CMD_WI_CONNECT, cmd_connect },
	{ CMD_WI_NEUTRAL, cmd_neutral },
	{ CMD_WI_DETACH,  cmd_detach  },
	{ CMD_WI_RESUME,  cmd_resume  }
};

static int dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd < commands + ARRAY_SIZE(commands); cd++)
		if(cd->cmd != cmd)
			continue;
		else if((ret = cd->call(cn, msg)) > 0)
			return ret;
		else
			return reply(cn, ret);

	return reply(cn, -ENOSYS);
}

void handle_conn(struct conn* cn)
{
	int ret, fd = cn->fd;
	struct ucmsg* msg;

	if((ret = uc_recv_whole(fd, rxbuf, sizeof(rxbuf))) < 0)
		goto err;
	if(!(msg = uc_msg(rxbuf, ret)))
		goto err;
	if((ret = dispatch_cmd(cn, msg)) >= 0)
		return;
err:
	drop_connection(cn);
}

void setup_control(void)
{
	const int flags = SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = WICTL
	};
	int fd, ret;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", addr.path, ret);
	if((ret = sys_listen(fd, 1)))
		quit("listen", addr.path, ret);

	ctrlfd = fd;
}

void exit_control(void)
{
	sys_unlink(WICTL);
	_exit(0xFF);
}

void quit(const char* msg, char* arg, int err)
{
	warn(msg, arg, err);
	exit_control();
}

void handle_control(void)
{
	int cfd, sfd = ctrlfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	int flags = SOCK_NONBLOCK;
	struct conn *cn;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0)
		if((cn = grab_conn_slot()))
			cn->fd = cfd;
		else
			sys_close(cfd);
}
