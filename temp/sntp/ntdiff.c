#include <bits/socket/packet.h>
#include <bits/socket/inet.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ppoll.h>
#include <sys/sched.h>

#include <main.h>
#include <format.h>
#include <endian.h>
#include <printf.h>
#include <string.h>
#include <util.h>

ERRTAG("ntdiff");

/* This is a sketch for small-scale filtering. Runs several
   requests (at 1-second intervals) against given server and
   dumps some statistics on that.

   The goal here is figuring out what kind of data to expect
   in real-world scenarios with public servers. */

#define NS 5

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

struct stats {
	int64_t avg_dt;
	int64_t med_dt;
	int64_t std_dt;
	int64_t avg_rt;
	int64_t med_rt;
	int64_t std_rt;
};

struct top {
	byte addr[24];
	uint addrlen;
	int fd;

	int n;
	int64_t dt[NS];
	int64_t rt[NS];
	struct timespec ts[NS];
	int idx[NS];
};

#define CTX struct top* ctx __attribute__((unused))

static char* fmt_xsec(char* p, char* e, int64_t dt)
{
	uint64_t ad = dt < 0 ? -dt : dt;
	uint64_t as = ad >> 32;
	uint32_t sec = as;
	uint32_t nsec = ((ad - as) * 1000000000) / (1ULL << 32);
	uint32_t usec = nsec / 1000;
	uint32_t msec = usec / 1000;

	if(sec) {
		p = fmtint(p, e, sec);
		if(sec < 10) {
			p = fmtchar(p, e, '.');
			p = fmtint(p, e, msec / 1000);
		}
		p = fmtchar(p, e, 's');
	} else if(msec) {
		p = fmtint(p, e, msec);
		if(msec < 10) {
			p = fmtchar(p, e, '.');
			p = fmtint(p, e, (usec - 1000*msec)/100);
		}
		p = fmtstr(p, e, "ms");
	} else if(usec) {
		p = fmtint(p, e, usec);
		if(usec < 10) {
			p = fmtchar(p, e, '.');
			p = fmtint(p, e, (nsec - 1000*usec)/100);
		}
		p = fmtstr(p, e, "us");
	} else {
		p = fmtint(p, e, nsec);
		p = fmtstr(p, e, "ns");
	}

	return p;
}

static char* fmt_dt(char* p, char* e, int64_t dt)
{
	if(dt < 0)
		p = fmtchar(p, e, '-');
	else if(dt > 0)
		p = fmtchar(p, e, '+');

	return fmt_xsec(p, e, dt);
}

static char* fmt_rt(char* p, char* e, int64_t dt)
{
	if(dt < 0)
		p = fmtchar(p, e, '-');

	return fmt_xsec(p, e, dt);
}

static void dump_data_point(int i, int64_t dt, int64_t rt, struct timespec* ts)
{
	FMTBUF(p, e, buf, 100);
	p = fmtint(p, e, i);
	p = fmtchar(p, e, ' ');
	p = fmtlong(p, e, ts->sec);
	p = fmtchar(p, e, ' ');
	p = fmtpad(p, e, 8, fmt_dt(p, e, dt));
	p = fmtchar(p, e, ' ');
	p = fmtpad(p, e, 8, fmt_rt(p, e, rt));
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static inline int ntp_bits(uint32_t code, int from, int bits)
{
	return ((code >> (32 - from - bits)) & ((1 << bits) - 1));
}

static void put_system_time(uint64_t* nt)
{
	int ret;
	struct timespec ts;

	if((ret = sys_clock_gettime(CLOCK_REALTIME, &ts)) < 0)
		fail("clock_gettime", "REALTIME", ret);

	uint64_t ns = ts.nsec;
	uint64_t fs = ns*(1ULL<<32)/(1000000000ULL);
	uint64_t ss = ts.sec + 2208988800UL;

	*nt = (ss << 32) | fs;
}

static void pause(int sec)
{
	struct timespec ts = { sec, 0 };

	sys_nanosleep(&ts, 0);
}

static int get_data_point(CTX)
{
	struct ntpreq req, *rq = &req;
	struct timespec bt;
	uint64_t sent, recv;
	int i = ctx->n;

	void* addr = ctx->addr;
	int addrlen = ctx->addrlen;
	byte from[sizeof(ctx->addr)];
	int fromlen = sizeof(from);
	int ret, fd = ctx->fd;
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	struct timespec ts = { 1, 0 };

	if((ret = sys_clock_gettime(CLOCK_BOOTTIME, &bt)) < 0)
		fail("clock_gettime", "BOOTTIME", ret);

	memzero(&req, sizeof(req));

	put_system_time(&sent);

	uint64_t reference = htonx(sent);

	rq->code = htonl((4 << 27) | (3 << 24));
	rq->transmit = reference;

	if((ret = sys_sendto(fd, rq, sizeof(*rq), 0, addr, addrlen)) < 0) {
		warn("send", NULL, ret);
		return ret;
	}
recv:
	if((ret = sys_ppoll(&pfd, 1, &ts, NULL)) < 0) {
		fail("ppoll", NULL, ret);
	} else if(ret == 0) {
		warn("timeout", NULL, 0);
		return -ETIMEDOUT;
	} if((ret = sys_recvfrom(fd, rq, sizeof(*rq), 0, &from, &fromlen)) < 0) {
		fail("recv", NULL, ret);
	}

	if(fromlen != addrlen)
		goto recv;
	if(memcmp(addr, from, addrlen))
		goto recv;
	if(rq->originate != reference)
		goto recv;

	put_system_time(&recv);

	uint64_t T1 = sent;
	uint64_t T2 = ntohx(rq->receive);
	uint64_t T3 = ntohx(rq->transmit);
	uint64_t T4 = recv;

	int64_t rt = (T4 - T1) - (T3 - T2);
	int64_t dt = (T2 - T1) + (T3 - T4);
	dt /= 2;

	ctx->dt[i] = dt;
	ctx->rt[i] = rt;
	ctx->ts[i] = bt;
	ctx->idx[i] = i;
	ctx->n++;

	dump_data_point(i, dt, rt, &bt);

	return 0;
}

static int64_t average(int64_t* x, int n)
{
	int64_t s = 0;

	for(int i = 0; i < n; i++)
		s += x[i];

	return s/n;
}

static int64_t median(int64_t* x, int n)
{
	int64_t q[n];

	memcpy(q, x, n*sizeof(*x));

	for(int i = 0; i < n - 1; i++) {
		int m = i;

		for(int j = i + 1; j < n; j++)
			if(q[j] < q[i])
				m = j;

		int64_t t = q[i];
		q[i] = q[m];
		q[m] = t;
	}

	return q[n/2];
}

static inline int64_t iabs(int64_t x)
{
	return x < 0 ? -x : x;
}

static inline int64_t isqr(int64_t x)
{
	return x*x;
}

static int64_t isqrt(int64_t x)
{
	if(x < 0)
		fail("negative isqrt", NULL, 0);

	int sh = 2;
	int64_t sx = x >> sh;

	while(sx && sx != x)
		sx = (x >> (sh += 2));

	sh -= 2;

	int64_t rt = 0;
	while(sh >= 0) {
		rt = rt << 1;
		int64_t cr = rt + 1;

		if(cr*cr <= (x >> sh))
			rt = cr;

		sh -= 2;
	}

	return rt;
}

static int64_t deviation(int64_t* x, int n, int64_t avg)
{
	int64_t s = 0;

	if(n < 2)
		return 0;

	for(int i = 0; i < n; i++)
		s += isqr(x[i] - avg);

	return isqrt(s / (n - 1));
}

static void empty_line(void)
{
	char* nl = "\n";

	writeall(STDOUT, nl, 1);
}

static void dump_dtline(CTX, struct stats* st)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "dt");
	p = fmtstr(p, e, " avg ");
	p = fmt_dt(p, e, st->avg_dt);
	p = fmtstr(p, e, " med ");
	p = fmt_dt(p, e, st->med_dt);
	p = fmtstr(p, e, " std ");
	p = fmt_rt(p, e, st->std_dt);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void dump_rtline(CTX, struct stats* st)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "rt");
	p = fmtstr(p, e, " avg ");
	p = fmt_rt(p, e, st->avg_rt);
	p = fmtstr(p, e, " med ");
	p = fmt_rt(p, e, st->med_rt);
	p = fmtstr(p, e, " std ");
	p = fmt_rt(p, e, st->std_rt);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void dump_stats(CTX, struct stats* st)
{
	int n = ctx->n;
	int64_t* dt = ctx->dt;
	int64_t* rt = ctx->rt;

	if(ctx->n < 2)
		fail("too noisy", NULL, 0);

	st->avg_dt = average(dt, n);
	st->med_dt = median(dt, n);
	st->std_dt = deviation(dt, n, st->avg_dt);
	st->avg_rt = average(rt, n);
	st->med_rt = median(rt, n);
	st->std_rt = deviation(rt, n, st->avg_rt);

	dump_dtline(ctx, st);
	dump_rtline(ctx, st);
}

static void dump_table(CTX, struct stats* st)
{
	int n = ctx->n;
	int64_t* dt = ctx->dt;
	int64_t* rt = ctx->rt;
	int* idx = ctx->idx;

	int64_t avg_dt = st->avg_dt;
	int64_t med_dt = st->med_dt;
	int64_t avg_rt = st->avg_rt;
	int64_t med_rt = st->med_rt;

	for(int i = 0; i < n; i++) {
		int64_t dti = dt[i];
		int64_t rti = rt[i];

		FMTBUF(p, e, buf, 100);
		p = fmtint(p, e, idx[i]);
		p = fmtchar(p, e, ' ');
		p = fmtpad(p, e, 8, fmt_dt(p, e, dti));
		p = fmtchar(p, e, ' ');
		p = fmtpad(p, e, 8, fmt_dt(p, e, dti - avg_dt));
		p = fmtchar(p, e, ' ');
		p = fmtpad(p, e, 8, fmt_dt(p, e, dti - med_dt));
		p = fmtchar(p, e, ' ');
		p = fmtpad(p, e, 8, fmt_rt(p, e, rti));
		p = fmtchar(p, e, ' ');
		p = fmtpad(p, e, 8, fmt_rt(p, e, rti - avg_rt));
		p = fmtchar(p, e, ' ');
		p = fmtpad(p, e, 8, fmt_rt(p, e, rti - med_rt));
		FMTENL(p, e);

		writeall(STDOUT, buf, p - buf);
	}
}

static void filter_data(CTX, struct stats* st)
{
	int n = ctx->n;
	int64_t* dt = ctx->dt;
	int64_t* rt = ctx->rt;
	int mark[n];
	int64_t avg_dt = st->avg_dt;
	int64_t std_dt = st->std_dt;
	int64_t avg_rt = st->avg_rt;
	int64_t std_rt = st->std_rt;

	memzero(mark, sizeof(mark));

	for(int i = 0; i < n; i++) {
		if(iabs(dt[i] - avg_dt) > std_dt)
			continue;
		if(iabs(rt[i] - avg_rt) > std_rt)
			continue;

		mark[i] = 1;
	}

	int j = 0;

	for(int i = 0; i < n; i++) {
		if(!mark[i])
			continue;

		ctx->dt[j] = ctx->dt[i];
		ctx->rt[j] = ctx->rt[i];
		ctx->ts[j] = ctx->ts[i];
		ctx->idx[j] = ctx->idx[i];
		j++;
	}

	ctx->n = j;
}

static void dump_final(CTX)
{
	int64_t dt = average(ctx->dt, ctx->n);
	int64_t rt = average(ctx->rt, ctx->n);

	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "dt ");
	p = fmt_dt(p, e, dt);
	p = fmtstr(p, e, " rt ");
	p = fmt_rt(p, e, rt);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void process_data_points(CTX)
{
	struct stats st;

	memzero(&st, sizeof(st));

	empty_line();
	dump_stats(ctx, &st);
	empty_line();
	dump_table(ctx, &st);

	filter_data(ctx, &st); memzero(&st, sizeof(st));

	empty_line();
	dump_stats(ctx, &st);
	empty_line();
	dump_table(ctx, &st);

	empty_line();
	dump_final(ctx);
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

static void run_req_sequence(CTX)
{
	int i;

	for(i = 0; i < NS + 3; i++) {
		if(ctx->n >= NS)
			break;
		if(i > 0)
			pause(1);

		get_data_point(ctx);
	}
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(argc != 2)
		fail("bad call", NULL, 0);

	prep_server(ctx, argv[1]);

	run_req_sequence(ctx);

	process_data_points(ctx);

	return 0;

}
