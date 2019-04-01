#include <bits/socket/packet.h>
#include <bits/socket/inet.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/sched.h>
#include <sys/ppoll.h>

#include <main.h>
#include <format.h>
#include <endian.h>
#include <printf.h>
#include <string.h>
#include <util.h>

/* This tool continuously sends packets at 1-second intervals
   and dumps (bt, dt) pairs, where dt is the NTP clock offset and
   bt is CLOCK_BOOTTIME taken just before sending the request.

   It is mostly meant for monitoring/measuring/plotting systematic
   error (clock drift), possibly in real time while observing the
   effects of adjtimex and such.

   Negative offset here means our local clock is behind.
   (in line with timed, opposite to RFC 4330)

   DO NOT RUN THIS AGAINST PUBLIC SERVERS. */

ERRTAG("ntping");

struct ntpreq {
	uint32_t code;
	uint32_t rdelay;
	uint32_t rdispersion;
	byte refid[4];
	uint64_t reference;
	uint64_t originate;
	uint64_t receive;
	uint64_t transmit;
} __attribute__((packed));

struct top {
	byte addr[24];
	uint addrlen;
	int fd;

	uint64_t T1;
	uint64_t T2;
	uint64_t T3;
	uint64_t T4;

	struct timespec ref;
	struct timespec adj;
};

#define CTX struct top* ctx __attribute__((unused))

static void dump_date_line(CTX, int64_t dt)
{
	struct timespec* ts = &ctx->ref;

	uint64_t ad = dt < 0 ? -dt : dt;
	uint64_t as = ad >> 32;
	uint32_t sec = as;
	uint32_t nsec = ((ad - as) * 1000000000) / (1ULL << 32);
	uint32_t usec = nsec / 1000;
	//uint32_t msec = usec / 1000;

	FMTBUF(p, e, buf, 100);

	p = fmtlong(p, e, ts->sec);
	p = fmtchar(p, e, '.');
	p = fmtpad0(p, e, 9, fmtlong(p, e, ts->nsec));
	p = fmtchar(p, e, ' ');

	if(dt < 0) p = fmtchar(p, e, '-');
	p = fmtlong(p, e, sec);
	p = fmtchar(p, e, '.');
	p = fmtpad0(p, e, 6, fmtlong(p, e, usec));

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static inline int ntp_bits(uint32_t code, int from, int bits)
{
	return ((code >> (32 - from - bits)) & ((1 << bits) - 1));
}

static void put_system_time(struct timespec* ts, uint64_t* nt)
{
	int ret;

	if((ret = sys_clock_gettime(0, ts)) < 0)
		fail("clock_gettime", NULL, ret);

	uint64_t ns = ts->nsec;
	uint64_t fs = ns*(1ULL<<32)/(1000000000ULL);
	uint64_t ss = ts->sec + 2208988800UL;

	*nt = (ss << 32) | fs;
}

static void normalize(struct timespec* d)
{
	long nsec_in_sec = 1000000000;

	if(d->nsec < 0) {
		d->sec--;
		d->nsec += nsec_in_sec;
	} else if(d->nsec > nsec_in_sec) {
		d->sec++;
		d->nsec -= nsec_in_sec;
	}
}

static void sub_ts(struct timespec* d, struct timespec* a, struct timespec* b)
{
	d->sec = a->sec - b->sec;
	d->nsec = a->nsec - b->nsec;

	normalize(d);
}

static void inc_ts(struct timespec* d, struct timespec* a)
{
	struct timespec r;

	r.sec = d->sec + a->sec;
	r.nsec = d->nsec + a->nsec;

	normalize(&r);

	*d = r;
}

static void adjust_timeout(CTX, struct timespec* tx, struct timespec* ts)
{
	struct timespec* rf = &ctx->ref;
	struct timespec* ad = &ctx->adj;
	struct timespec el, dt;

	if(rf->sec || rf->nsec) {
		sub_ts(&el, tx, rf);
		sub_ts(&dt, ts, &el);
		inc_ts(ad, &dt);
		inc_ts(ts, ad);
	}

	ctx->ref = *tx;
}

static void send_recv_packet(CTX)
{
	struct ntpreq req, *rq = &req;
	uint64_t sent, recv;

	void* addr = ctx->addr;
	int addrlen = ctx->addrlen;
	byte from[sizeof(ctx->addr)];
	int fromlen = sizeof(from);
	int ret, fd = ctx->fd;
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	struct timespec ts = { 1, 0 };
	struct timespec rx, tx;

	memzero(&req, sizeof(req));

	put_system_time(&tx, &sent);
	adjust_timeout(ctx, &tx, &ts);

	uint64_t reference = htonx(sent);

	rq->code = htonl((4 << 27) | (3 << 24));
	rq->transmit = reference;

	if((ret = sys_sendto(fd, rq, sizeof(*rq), 0, addr, addrlen)) < 0)
		fail("send", NULL, ret);
recv:
	if((ret = sys_ppoll(&pfd, 1, &ts, NULL)) < 0)
		fail("ppoll", NULL, ret);
	else if(ret == 0)
		return;
	if((ret = sys_recvfrom(fd, rq, sizeof(*rq), 0, &from, &fromlen)) < 0)
		fail("recv", NULL, ret);

	if(fromlen != addrlen)
		goto recv;
	if(memcmp(addr, from, addrlen))
		goto recv;
	if(rq->originate != reference)
		goto recv;

	put_system_time(&rx, &recv);

	uint64_t T1 = sent;
	uint64_t T2 = ntohx(rq->receive);
	uint64_t T3 = ntohx(rq->transmit);
	uint64_t T4 = recv;

	int64_t dt = (T2 - T1) + (T3 - T4);
	dt = -dt/2;

	dump_date_line(ctx, dt);

	if((ret = sys_ppoll(NULL, 0, &ts, NULL)) < 0)
		fail("ppoll", "timeout", ret);
}

static void prep_server(CTX, char* address)
{
	struct sockaddr_in* dst = (void*)ctx->addr;
	int port = 123;
	int fd;
	char* p;

	dst->family = AF_INET;

	if(!(p = parseip(address, dst->addr)))
		fail("bad ip:", address, 0);
	if(*p == ':' && !(p = parseint(p + 1, &port)))
		fail("bad port:", address, 0);
	if(*p)
		fail("bad address:", address, 0);

	dst->port = htons(port);

	if((fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		fail("socket", "udp", fd);

	ctx->fd = fd;
	ctx->addrlen = sizeof(*dst);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(argc != 2)
		fail("bad call", NULL, 0);

	prep_server(ctx, argv[1]);

	while(1) send_recv_packet(ctx);
}
