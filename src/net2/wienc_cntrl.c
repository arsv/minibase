#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/sched.h>
#include <sys/mman.h>

#include <nlusctl.h>
#include <heap.h>
#include <util.h>

#include "common.h"
#include "wienc.h"

int ctrlfd;

static char rxbuf[100];
static char txbuf[100];
struct ucbuf uc;

#define CN struct conn* cn
#define MSG struct ucmsg* msg

#define REPLIED 1

static void send_report(char* buf, int len)
{
	struct conn* cn;
	int fd;

	for(cn = conns; cn < conns + nconns; cn++) {
		if(!cn->rep || (fd = cn->fd) <= 0)
			continue;

		struct itimerval old, itv = {
			.interval = { 0, 0 },
			.value = { 1, 0 }
		};

		sys_setitimer(ITIMER_REAL, &itv, &old);

		if(sys_write(fd, buf, len) < 0)
			sys_shutdown(fd, SHUT_RDWR);

		sys_setitimer(ITIMER_REAL, &old, NULL);
	}
}

static void report_simple(int cmd)
{
	char buf[64];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, cmd);
	uc_put_end(&uc);

	send_report(uc.brk, uc.ptr - uc.brk);
}

void report_net_down(void)
{
	report_simple(REP_WI_NET_DOWN);
}

void report_scanning(void)
{
	report_simple(REP_WI_SCANNING);
}

void report_scan_done(void)
{
	report_simple(REP_WI_SCAN_DONE);
}

void report_scan_fail(void)
{
	report_simple(REP_WI_SCAN_FAIL);
}

void report_no_connect(void)
{
	report_simple(REP_WI_NO_CONNECT);
}

static void report_station(int cmd)
{
	char buf[256];
	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, cmd);
	uc_put_bin(&uc, ATTR_BSSID, ap.bssid, sizeof(ap.bssid));
	uc_put_bin(&uc, ATTR_SSID, ap.ssid, ap.slen);
	uc_put_int(&uc, ATTR_FREQ, ap.freq);
	uc_put_end(&uc);

	send_report(uc.brk, uc.ptr - uc.brk);
}

void report_disconnect(void)
{
	report_station(REP_WI_DISCONNECT);
}

void report_connected(void)
{
	report_station(REP_WI_CONNECTED);
}

static void start_reply(int cmd)
{
	void* buf = NULL;
	int len;

	buf = txbuf;
	len = sizeof(txbuf);

	uc.brk = buf;
	uc.ptr = buf;
	uc.end = buf + len;

	uc_put_hdr(&uc, cmd);
}

static int send_reply(CN)
{
	uc_put_end(&uc);

	writeall(cn->fd, uc.brk, uc.ptr - uc.brk);

	return REPLIED;
}

static int reply(CN, int err)
{
	start_reply(err);

	return send_reply(cn);
}

static int estimate_status(void)
{
	int scansp = nscans*(sizeof(struct scan) + 10*sizeof(struct ucattr));

	return scansp + 128;
}

static void prep_heap(struct heap* hp, int size)
{
	hp->brk = sys_brk(NULL);

	size += (PAGE - size % PAGE) % PAGE;

	hp->ptr = hp->brk;
	hp->end = sys_brk(hp->brk + size);
}

static void free_heap(struct heap* hp)
{
	sys_brk(hp->brk);
}

static int common_wifi_state(void)
{
	if(authstate == AS_CONNECTED)
		return WS_CONNECTED;
	if(authstate == AS_NETDOWN)
		return rfkilled ? WS_RFKILLED : WS_NETDOWN;
	if(authstate != AS_IDLE)
		return WS_CONNECTING;
	if(scanstate != SS_IDLE)
		return WS_SCANNING;

	return WS_IDLE;
}

static void put_status_wifi(struct ucbuf* uc)
{
	uc_put_int(uc, ATTR_IFI, ifindex);
	uc_put_str(uc, ATTR_NAME, ifname);
	uc_put_int(uc, ATTR_STATE, common_wifi_state());

	if(authstate != AS_IDLE || ap.fixed)
		uc_put_bin(uc, ATTR_SSID, ap.ssid, ap.slen);
	if(authstate != AS_IDLE) {
		uc_put_bin(uc, ATTR_BSSID, ap.bssid, sizeof(ap.bssid));
		uc_put_int(uc, ATTR_FREQ, ap.freq);
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
		uc_put_bin(uc, ATTR_BSSID,  sc->bssid, sizeof(sc->bssid));
		uc_put_bin(uc, ATTR_SSID,   sc->ssid, sc->slen);

		if(!(sc->flags & SF_PASS))
			;
		else if(!(sc->flags & SF_GOOD))
			;
		else uc_put_flag(uc, ATTR_PRIO);

		uc_end_nest(uc, nn);
	}
}

static int cmd_status(CN, MSG)
{
	struct heap hp;

	prep_heap(&hp, estimate_status());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, 0);
	put_status_wifi(&uc);
	put_status_scans(&uc);
	uc_put_end(&uc);

	int ret = send_reply(cn);

	free_heap(&hp);

	cn->rep = 0;

	return ret;
}

static int cmd_scan(CN, MSG)
{
	int ret;

	if((ret = run_stamped_scan()) < 0)
		return ret;

	cn->rep = 1;

	return 0;
}

static int cmd_neutral(CN, MSG)
{
	int ret;

	opermode = OP_NEUTRAL;

	if((ret = start_disconnect()) < 0)
		return ret;

	cn->rep = 1;

	clr_timer();

	return 0;
}

static int configure_station(MSG)
{
	struct ucattr* assid;
	struct ucattr* apsk;

	reset_station();

	if(!(assid = uc_get(msg, ATTR_SSID)))
		return 0;

	byte* ssid = uc_payload(assid);
	int slen = uc_paylen(assid);

	if(!(apsk = uc_get(msg, ATTR_PSK)))
		return set_fixed_saved(ssid, slen);
	else if(uc_paylen(apsk) == 32)
		return set_fixed_given(ssid, slen, uc_payload(apsk));
	else
		return -EINVAL;
}

/* ACK to the command should preceed any notifications caused by the command.
   Since reassess_wifi_situation() is pretty long and involved piece of code,
   we reply early to make sure possible messages generated while starting
   connection do not confuse the client.

   Note at this point reassess_wifi_situation() does *not* generate any
   notifications. Not even SCANNING, that one is issued reactively on
   NL80211_CMD_TRIGGER_SCAN. */

static int cmd_connect(CN, MSG)
{
	int ret;

	if(authstate != AS_IDLE)
		return -EBUSY;
	if(scanstate != SS_IDLE)
		return -EBUSY;

	if((ret = configure_station(msg)) < 0)
		return ret;

	opermode = OP_ONESHOT;

	cn->rep = 1;

	ret = reply(cn, 0);

	clr_timer();

	reassess_wifi_situation();

	return ret;
}

static const struct cmd {
	int cmd;
	int (*call)(CN, MSG);
} commands[] = {
	{ CMD_WI_STATUS,  cmd_status  },
	{ CMD_WI_SCAN,    cmd_scan    },
	{ CMD_WI_NEUTRAL, cmd_neutral },
	{ CMD_WI_CONNECT, cmd_connect }
};

static int dispatch_cmd(CN, MSG)
{
	const struct cmd* cd;
	int cmd = msg->cmd;
	int ret;

	for(cd = commands; cd < commands + ARRAY_SIZE(commands); cd++)
		if(cd->cmd == cmd)
			break;
	if(!cd->cmd)
		ret = reply(cn, -ENOSYS);
	else if((ret = cd->call(cn, msg)) <= 0)
		ret = reply(cn, ret);

	return ret;
}

static void shutdown_conn(struct conn* cn)
{
	sys_shutdown(cn->fd, SHUT_RDWR);
}

void handle_conn(struct conn* cn)
{
	int ret, fd = cn->fd;

	struct urbuf ur = {
		.buf = rxbuf,
		.mptr = rxbuf,
		.rptr = rxbuf,
		.end = rxbuf + sizeof(rxbuf)
	};
	struct itimerval old, itv = {
		.interval = { 0, 0 },
		.value = { 1, 0 }
	};

	sys_setitimer(0, &itv, &old);

	while(1) {
		if((ret = uc_recv(fd, &ur, 0)) < 0)
			break;
		if((ret = dispatch_cmd(cn, ur.msg)) < 0)
			break;
	}

	if(ret < 0 && ret != -EBADF && ret != -EAGAIN)
		shutdown_conn(cn);

	sys_setitimer(0, &old, NULL);
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
		fail("listen", addr.path, ret);

	ctrlfd = fd;
}

void unlink_control(void)
{
	sys_unlink(WICTL);
}

void handle_control(void)
{
	int cfd, sfd = ctrlfd;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn *cn;

	while((cfd = sys_accept(sfd, &addr, &addr_len)) > 0)
		if((cn = grab_conn_slot()))
			cn->fd = cfd;
		else
			sys_close(cfd);
}
