#include <bits/socket/inet.h>
#include <bits/socket/inet6.h>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/random.h>

#include <endian.h>
#include <string.h>
#include <util.h>

#include "timed.h"

static inline int imax(int a, int b)
{
	return a > b ? a : b;
}

static int maybe_open_socket(CTX)
{
	int fd;

	if((fd = ctx->ntpfd) >= 0)
		return fd;

	if((fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		return fd;

	ctx->ntpfd = fd;
	ctx->pollready = 0;

	return fd;
}

static void maybe_close_socket(CTX)
{
	int state = ctx->state;
	int fd = ctx->ntpfd;

	if(fd < 0)
		return;

	if(state == TS_IDLE)
		;
	else if(state == TS_POLL_WAIT)
		;
	else return;

	sys_close(fd);

	ctx->ntpfd = -1;
	ctx->pollready = 0;
}

static uint64_t get_system_time(void)
{
	int ret;
	struct timespec ts;

	if((ret = sys_clock_gettime(CLOCK_REALTIME, &ts)) < 0)
		quit("clock_gettime", "REALTIME", ret);

	uint64_t ns = ts.nsec;
	uint64_t fs = ns*(1ULL<<32)/(1000000000ULL);
	uint64_t ss = ts.sec + 2208988800UL;

	return ((ss << 32) | fs);
}

static int put_server_address(CTX, void* buf, uint size)
{
	struct serv* sv = current(ctx);
	int alen;

	if(sv->flags & SF_IPv6) {
		struct sockaddr_in6* sa = buf;

		if(size < sizeof(*sa))
			return -ENOBUFS;

		sa->family = AF_INET6;
		sa->port = htons(sv->port);
		memcpy(sa->addr, sv->addr, 16);

		alen = sizeof(*sa);
	} else {
		struct sockaddr_in* sa = buf;

		if(size < sizeof(*sa))
			return -ENOBUFS;

		sa->family = AF_INET;
		sa->port = htons(sv->port);
		memcpy(sa->addr, sv->addr, 4);

		alen = sizeof(*sa);
	}

	return alen;
}

static int send_packet(CTX)
{
	int ret, fd;
	struct ntpreq rq;
	byte addr[32];
	int alen;

	if((alen = put_server_address(ctx, addr, sizeof(addr))) < 0)
		return alen;
	if((fd = maybe_open_socket(ctx)) < 0)
		return fd;

	uint64_t sendtime = get_system_time();
	uint64_t reference = htonx(sendtime);

	memzero(&rq, sizeof(rq));
	rq.code = htonl((4 << 27) | (3 << 24));
	rq.transmit = reference;

	if((ret = sys_sendto(fd, &rq, sizeof(rq), 0, addr, alen)) < 0)
		return ret;

	ctx->sendtime = sendtime;
	ctx->reference = reference;

	return 0;
}

static void ping_pass_complete(CTX)
{
	int idx = ctx->bestidx;

	if(idx < 0) {
		stop_service(ctx);
	} else {
		ctx->current = idx;

		consider_synching(ctx);

		int ival = imax(ctx->interval, 60);

		set_timed(ctx, TS_POLL_WAIT, ival);
	}
}

static void pick_next_server(CTX)
{
	int i, n = NSERVS;
	int rti = -1;
	uint rtt;

	for(i = 0; i < n; i++) {
		struct serv* sv = &ctx->servs[i];
		int flags = sv->flags;

		if(!(flags & SF_SET))
			continue;
		if(!(flags & SF_RTT))
			continue;
		if(flags & (SF_KILL | SF_FAIL))
			continue;

		if(rti < 0)
			;
		else if(rtt <= sv->rtt)
			continue;

		rti = i;
		rtt = sv->rtt;
	}

	if(rti < 0)
		return stop_service(ctx);

	ctx->current = rti;

	set_timed(ctx, TS_POLL_WAIT, 10);
}

static int switch_to_next(CTX)
{
	int i;

	for(i = 0; i < NSERVS; i++) {
		struct serv* sv = &ctx->servs[i];
		int flags = sv->flags;

		if(!(flags & SF_SET))
			continue;
		if(flags & (SF_RTT | SF_MARK | SF_KILL | SF_FAIL))
			continue;

		sv->flags |= SF_MARK;
		ctx->current = i;

		return i;
	}

	return -1;
}

static void mark_current(CTX, int flag)
{
	struct serv* sv = current(ctx);

	sv->flags |= flag;
}

static void drop_server(CTX, int flag)
{
	mark_current(ctx, flag);

	int state = ctx->state;

	if(state == TS_PING_SENT) {
		if(switch_to_next(ctx) >= 0)
			set_timed(ctx, TS_PING_WAIT, 1);
		else
			ping_pass_complete(ctx);
	} else if(state == TS_POLL_SENT) {
		pick_next_server(ctx);
	} else {
		quit("unexpected state", NULL, state);
	}
}

static void note_poll_time(CTX)
{
	struct timespec ts;
	int ret;

	if((ret = sys_clock_gettime(CLOCK_BOOTTIME, &ts)) < 0)
		quit("clock_gettime", "BOOTTIME", ret);

	ctx->polltime = ts.sec;
}

static void add_ping_point(CTX, uint64_t ref, int64_t lo, int64_t hi)
{
	struct serv* sv = current(ctx);
	int64_t rtt = (hi - lo);

	if(rtt < 0 || rtt > 0xFFFFFFFFLL)
		goto out;

	uint urtt = (uint)rtt;

	sv->rtt = urtt;
	sv->flags |= SF_RTT;

	if(ctx->bestidx < 0)
		;
	else if(urtt >= ctx->bestrtt)
		goto out;

	ctx->ref = ref;
	ctx->lo = lo;
	ctx->hi = hi;

	ctx->interval = 0;
	ctx->pollexp = 0;
	ctx->bestidx = ctx->current;
	ctx->bestrtt = urtt;

	note_poll_time(ctx);
out:
	if(switch_to_next(ctx) >= 0) {
		ctx->state = TS_PING_WAIT;
	} else {
		ping_pass_complete(ctx);
	}
}

static void add_poll_point(CTX, uint64_t ref, int64_t lo, int64_t hi)
{
	int64_t rtt = (hi - lo);

	if(rtt < 0)
		rtt = 0;
	else if(rtt > 0xFFFFFFFFLL)
		rtt = 0xFFFFFFFFLL;

	uint urtt = (uint)rtt;

	struct serv* sv = current(ctx);

	if(sv->rtt > urtt)
		sv->rtt = urtt;

	ctx->ref = ref;
	ctx->lo = lo;
	ctx->hi = hi;

	note_poll_time(ctx);

	consider_synching(ctx);

	int ival = imax(ctx->interval, 60);

	set_timed(ctx, TS_POLL_WAIT, ival);
}

static void handle_packet(CTX, struct ntpreq* rp)
{
	int state = ctx->state;

	uint code = ntohl(rp->code);
	uint li = (code >> 30) & 0x03;
	uint stratum = (code >> 16) & 0xFF;

	if(!stratum) /* kiss-of-death packet */
		return drop_server(ctx, SF_KILL);
	if(li == 3) /* not synchronized */
		return drop_server(ctx, SF_FAIL);

	ctx->failures = 0;

	uint64_t T1 = ctx->sendtime;
	uint64_t T2 = ntohx(rp->receive);
	uint64_t T3 = ntohx(rp->transmit);
	uint64_t T4 = get_system_time();

	uint64_t ref = T2 + (T3 - T2)/2;
	int64_t lo = T1 - T2;
	int64_t hi = T4 - T3;

	if(state == TS_PING_SENT)
		add_ping_point(ctx, ref, lo, hi);
	else if(state == TS_POLL_SENT)
		add_poll_point(ctx, ref, lo, hi);
	else
		warn("unexpected packet", NULL, 0);
}

static int valid_packet(CTX, struct ntpreq* rp)
{
	uint code = ntohl(rp->code);
	uint mode = (code >> 24) & 0x07;
	uint vn = (code >> 27) & 0x07;

	if((vn != 3 && vn != 4)) /* non-NTP message perhaps? */
		return 0;
	if(mode != 4) /* not a server msg? */
		return 0;
	if(rp->originate != ctx->reference) /* replay or late msg */
		return 0;

	ctx->reference = 0;

	return 1;
}

static struct ntpreq* recv_packet(CTX, void* buf, int len)
{
	int rd, fd = ctx->ntpfd;

	byte addr[ADDRLEN];
	int alen = sizeof(addr);
	byte bddr[ADDRLEN];
	int blen = sizeof(bddr);

	memzero(addr, sizeof(addr));
	memzero(bddr, sizeof(bddr));

	if((rd = sys_recvfrom(fd, buf, len, 0, addr, &alen)) < 0)
		return NULL;

	if(ctx->current < 0)
		return NULL;
	if((blen = put_server_address(ctx, bddr, blen)) < 0)
		return NULL;
	if(blen != alen)
		return NULL;
	if(memcmp(addr, bddr, alen))
		return NULL;

	if(rd < (int)sizeof(struct ntpreq))
		return NULL;

	return (struct ntpreq*)buf;
}

void check_packet(CTX)
{
	struct ntpreq* rp;
	byte buf[sizeof(*rp) + 50];

	if(!(rp = recv_packet(ctx, buf, sizeof(buf))))
		return;
	if(!valid_packet(ctx, rp))
		return;

	handle_packet(ctx, rp);

	maybe_close_socket(ctx);
}

static void send_ping_request(CTX)
{
	if(send_packet(ctx) >= 0) {
		set_timed(ctx, TS_PING_SENT, 1);
	} else if(ctx->failures < 1) {
		ctx->failures++;
		set_timed(ctx, TS_PING_WAIT, 1);
	} else if(switch_to_next(ctx) >= 0) {
		set_timed(ctx, TS_PING_WAIT, 1);
	} else {
		ping_pass_complete(ctx);
	}
}

static void handle_ping_timeout(CTX)
{
	if(ctx->failures < 1) {
		ctx->failures++;
		send_ping_request(ctx);
	} else if(switch_to_next(ctx) >= 0) {
		send_ping_request(ctx);
	} else {
		ping_pass_complete(ctx);
	}
}

static void send_poll_request(CTX)
{
	if(send_packet(ctx) >= 0) {
		/* success, wait for reply */
		set_timed(ctx, TS_POLL_SENT, 1);
	} else if(ctx->failures < 3) {
		/* retry in 10s, noting a failure */
		ctx->failures++;
		set_timed(ctx, TS_POLL_WAIT, 10);
	} else {
		/* we're out of retries with this server */
		/* drop it and check if we've got spares */
		mark_current(ctx, SF_FAIL);
		pick_next_server(ctx);
	}
}

static void handle_poll_timeout(CTX)
{
	if(ctx->failures < 3) {
		ctx->failures++;
		set_timed(ctx, TS_POLL_WAIT, 10 - 1);
	} else {
		set_timed(ctx, TS_SELECT, 10);
	}
}

static void start_selection(CTX)
{
	for(int i = 0; i < NSERVS; i++)
		ctx->servs[i].flags &= ~SF_MARK;

	if(ctx->current >= 0) {
		struct serv* sv = current(ctx);
		ctx->bestidx = ctx->current;
		ctx->bestrtt = sv->rtt;
	} else {
		ctx->bestidx = -1;
		ctx->bestrtt = 0;
	}

	if(switch_to_next(ctx) >= 0) {
		send_ping_request(ctx);
	} else {
		ping_pass_complete(ctx);
	}
}

void handle_timeout(CTX)
{
	int state = ctx->state;

	if(state == TS_SELECT)
		start_selection(ctx);
	else if(state == TS_PING_SENT)
		handle_ping_timeout(ctx);
	else if(state == TS_POLL_SENT)
		handle_poll_timeout(ctx);
	else if(state == TS_PING_WAIT)
		send_ping_request(ctx);
	else if(state == TS_POLL_WAIT)
		send_poll_request(ctx);
	else
		warn("unexpected timeout state", NULL, state);

	maybe_close_socket(ctx);
}
