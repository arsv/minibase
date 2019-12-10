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
#define MSG struct ucattr* msg __unused

#define REPLIED 1

void close_conn(CN)
{
	int fd = cn->fd;
	
	if(fd < 0) return;

	sys_close(fd);

	memzero(cn, sizeof(*cn));

	pollset = 0;
}

static void send_timed(struct conn* cn, struct ucbuf* uc)
{
	int ret, fd = cn->fd;

	if(!cn->rep || fd < 0)
		return;

	if((ret = uc_send(fd, uc)) > 0)
		return;
	else if(ret != -EAGAIN)
		goto drop;
	if((ret = uc_wait_writable(fd)) < 0)
		goto drop;
	if((ret = uc_send(fd, uc)) > 0)
		return;
drop:
	close_conn(cn);
}

static void send_report(struct ucbuf* uc)
{
	struct conn* cn;

	for(cn = conns; cn < conns + nconns; cn++) {
		send_timed(cn, uc);
	}
}

static void report_simple(int cmd)
{
	char buf[64];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);

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

	send_report(&uc);
}

void report_scan_end(int err)
{
	char buf[64];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, REP_SCAN_END);
	if(err) uc_put_int(&uc, ATTR_ERROR, err);

	send_report(&uc);
}

void report_disconnect(void)
{
	report_station(REP_DISCONNECT);
}

void report_no_connect(void)
{
	report_simple(REP_NO_CONNECT);
}

void report_link_ready(void)
{
	report_station(REP_LINK_READY);
}

static int send_reply(CN, struct ucbuf* uc)
{
	int ret, fd = cn->fd;

	if((ret = uc_wait_writable(fd)) < 0)
		return ret;
	if((ret = uc_send(fd, uc)) > 0)
		return ret;

	close_conn(cn);

	return REPLIED;
}

static int send_multi(struct conn* cn, struct ucbuf* uc, void* tail, int tlen)
{
	int ret, fd = cn->fd;
	struct iovec iov[2];

	iov[1].base = tail;
	iov[1].len = tlen;

	if((ret = uc_iov_hdr(&iov[0], uc)) < 0)
		goto drop;
	if((ret = uc_wait_writable(fd)) < 0)
		goto drop;
	if((ret = uc_send_iov(fd, iov, 2)) > 0)
		return ret;
drop:
	close_conn(cn);

	return REPLIED;
}

static int reply(CN, int err)
{
	struct ucbuf uc;

	uc_buf_set(&uc, txbuf, sizeof(txbuf));
	uc_put_hdr(&uc, err);

	return send_reply(cn, &uc);
}

static int cmd_status(CN, MSG)
{
	struct ucbuf uc;
	char buf[256];
	int tm;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	if(ifindex <= 0)
		goto send; /* no active device */

	uc_put_int(&uc, ATTR_IFI, ifindex);
	uc_put_str(&uc, ATTR_NAME, ifname);
	uc_put_int(&uc, ATTR_STATE, operstate);

	if(ap.slen)
		uc_put_bin(&uc, ATTR_SSID, ap.ssid, ap.slen);
	if((tm = time_to_scan()) >= 0)
		uc_put_int(&uc, ATTR_TIME, tm);

	if(!ap.freq)
		goto send; /* not connected to particular AP */

	uc_put_bin(&uc, ATTR_BSSID, ap.bssid, sizeof(ap.bssid));
	uc_put_int(&uc, ATTR_FREQ, ap.freq);
send:
	return send_reply(cn, &uc);
}

static int first_nomempty(int start)
{
	if(start < 0)
		return -EINVAL;

	for(int i = start; i < nscans; i++)
		if(scans[i].freq)
			return i;

	return -ENOENT;
}

static int cmd_getscan(CN, MSG)
{
	struct ucbuf uc;
	char buf[256];

	int* start = uc_get_int(msg, ATTR_NEXT);
	int idx, from = start ? *start : 0;

	if((idx = first_nomempty(from)) < 0)
		return idx;

	struct scan* sc = &scans[idx];
	
	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	uc_put_int(&uc, ATTR_NEXT, idx + 1);
	uc_put_int(&uc, ATTR_FREQ, sc->freq);
	uc_put_int(&uc, ATTR_SIGNAL, sc->signal);
	uc_put_bin(&uc, ATTR_BSSID, sc->bssid, sizeof(sc->bssid));

	if(!sc->ies) {
		return send_reply(cn, &uc);
	} else {
		uc_put_tail(&uc, ATTR_IES, sc->ieslen);

		return send_multi(cn, &uc, sc->ies, sc->ieslen);
	}
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

static int cmd_runscan(CN, MSG)
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
	int ret;

	if(ifindex <= 0)
		return -ENODEV;

	cn->rep = 1;

	if((ret = ap_resume()) < 0)
		return ret;

	return 0;
}

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
	{ CMD_STATUS,  cmd_status  },
	{ CMD_SETDEV,  cmd_setdev  },
	{ CMD_RUNSCAN, cmd_runscan },
	{ CMD_GETSCAN, cmd_getscan },
	{ CMD_CONNECT, cmd_connect },
	{ CMD_NEUTRAL, cmd_neutral },
	{ CMD_DETACH,  cmd_detach  },
	{ CMD_RESUME,  cmd_resume  }
};

static int dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = uc_repcode(msg);
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
	struct ucattr* msg;

	if((ret = uc_recv(fd, rxbuf, sizeof(rxbuf))) < 0)
		goto err;
	if(!(msg = uc_msg(rxbuf, ret)))
		goto err;
	if((ret = dispatch_cmd(cn, msg)) >= 0)
		return;
err:
	close_conn(cn);
}

void setup_control(void)
{
	const int flags = SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC;
	char* path = CONTROL;
	int fd, ret;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = uc_listen(fd, path, 5)) < 0)
		fail("ucbind", path, ret);

	ctrlfd = fd;
}

void handle_control(void)
{
	int cfd, sfd = ctrlfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	int flags = SOCK_NONBLOCK;
	struct conn *cn;

	while((cfd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if((cn = grab_conn_slot()))
			cn->fd = cfd;
		else
			sys_close(cfd);
	}
}
