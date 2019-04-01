#include <bits/socket/packet.h>
#include <bits/socket/inet.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timer.h>

#include <main.h>
#include <format.h>
#include <endian.h>
#include <printf.h>
#include <string.h>
#include <util.h>

/* A simple send-recv with a full packet dump. This is mostly for
   figuring out how NTP packets look like, and making sure I parse
   them correctly. */

ERRTAG("ntwhat");

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
};

#define CTX struct top* ctx __attribute__((unused))

static char* fmt_ts(char* p, char* e, uint64_t ts)
{
	uint32_t sec = (uint32_t)(ts >> 32);
	uint32_t frac = (uint32_t)(ts & 0xFFFFFFFF);

	p = fmtpad0(p, e, 8, fmtx32(p, e, sec));
	p = fmtstr(p, e, ".");
	p = fmtpad0(p, e, 8, fmtx32(p, e, frac));
	p = fmtstr(p, e, " (");
	p = fmtu32(p, e, sec);
	p = fmtstr(p, e, "s)");

	return p;
}

static char* fmt_hexdt(char* p, char* e, int64_t dt)
{
	p = fmtpad0(p, e, 8, fmtx32(p, e, (dt >> 32) & 0xFFFFFFFF));
	p = fmtchar(p, e, '.');
	p = fmtpad0(p, e, 8, fmtx32(p, e, dt & 0xFFFFFFFF));

	return p;
}

static char* fmt_secdt(char* p, char* e, int64_t dt)
{
	uint64_t ad = dt < 0 ? -dt : dt;
	uint64_t as = ad >> 32;
	uint32_t sec = as;
	uint32_t nsec = ((ad - as) * 1000000000) / (1ULL << 32);
	uint32_t usec = nsec / 1000;
	uint32_t msec = usec / 1000;

	if(dt < 0)
		p = fmtchar(p, e, '-');

	if(sec) {
		p = fmtint(p, e, sec);
		if(sec < 10) {
			p = fmtchar(p, e, '.');
			p = fmtint(p, e, msec / 1000);
		}
		p = fmtchar(p, e, 's');
	} else if(msec) {
		p = fmtint(p, e, msec);
		p = fmtstr(p, e, "ms");
	} else if(usec) {
		p = fmtint(p, e, usec);
		p = fmtstr(p, e, "us");
	} else {
		p = fmtint(p, e, nsec);
		p = fmtstr(p, e, "ns");
	}

	return p;
}

static char* fmt_dt(char* p, char* e, int64_t dt)
{
	p = fmt_hexdt(p, e, dt);
	p = fmtstr(p, e, " (");
	p = fmt_secdt(p, e, dt);
	p = fmtstr(p, e, ")");

	return p;
}

static inline int ntp_bits(uint32_t code, int from, int bits)
{
	return ((code >> (32 - from - bits)) & ((1 << bits) - 1));
}

static void dump_packet(struct ntpreq* rq)
{
	uint32_t code = ntohl(rq->code);

	FMTBUF(p, e, buf, 1000);

	p = fmtstr(p, e, "li ");
	p = fmtint(p, e, ntp_bits(code, 0, 2));
	p = fmtstr(p, e, " vn ");
	p = fmtint(p, e, ntp_bits(code, 2, 3));
	p = fmtstr(p, e, " mode ");
	p = fmtint(p, e, ntp_bits(code, 5, 3));
	p = fmtstr(p, e, " stratum ");
	p = fmtint(p, e, ntp_bits(code, 8, 8));
	p = fmtstr(p, e, " poll ");
	p = fmtint(p, e, ntp_bits(code, 16, 8));
	p = fmtstr(p, e, " prec ");
	p = fmtint(p, e, (int8_t)ntp_bits(code, 24, 8));
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "rdelay ");
	p = fmtu32(p, e, ntohl(rq->rdelay));
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "rdispersion ");
	p = fmtu32(p, e, ntohl(rq->rdispersion));
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "refid ");
	p = fmtip(p, e, rq->refid);
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "refr ");
	p = fmt_ts(p, e, ntohx(rq->reference));
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "orig ");
	p = fmt_ts(p, e, ntohx(rq->originate));
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "recv ");
	p = fmt_ts(p, e, ntohx(rq->receive));
	p = fmtstr(p, e, "\n");

	p = fmtstr(p, e, "xmit ");
	p = fmt_ts(p, e, ntohx(rq->transmit));
	p = fmtstr(p, e, "\n");

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void dump_dt(const char* pref, int64_t dt)
{
	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, pref);
	p = fmt_dt(p, e, dt);

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void put_system_time(uint64_t* nt)
{
	struct timespec ts;
	int ret;

	if((ret = sys_clock_gettime(0, &ts)) < 0)
		fail("clock_gettime", NULL, ret);

	uint64_t ns = ts.nsec;
	uint64_t fs = ns*(1ULL<<32)/(1000000000ULL);
	uint64_t ss = ts.sec + 2208988800UL;

	*nt = (ss << 32) | fs;
}

static void send_recv_packet(CTX, struct ntpreq* rq)
{
	void* addr = ctx->addr;
	int addrlen = ctx->addrlen;
	byte from[sizeof(ctx->addr)];
	int fromlen = sizeof(from);
	int ret, fd = ctx->fd;

	sys_alarm(1);

	if((ret = sys_sendto(fd, rq, sizeof(*rq), 0, addr, addrlen)) < 0)
		fail("send", NULL, ret);
	if((ret = sys_recvfrom(fd, rq, sizeof(*rq), 0, &from, &fromlen)) < 0)
		fail("recv", NULL, ret);

	if(fromlen != addrlen)
		fail("return address of invalid size", NULL, 0);
	if(memcmp(addr, from, addrlen))
		fail("return address differs", NULL, 0);
}

static void run_ntp_query(CTX)
{
	struct ntpreq req, *rq = &req;
	uint64_t sent, recv;

	put_system_time(&sent);

	memzero(&req, sizeof(req));
	req.code = htonl((4 << 27) | (3 << 24));
	req.transmit = htonx(sent);

	dump_packet(rq);

	send_recv_packet(ctx, rq);

	dump_packet(rq);
	put_system_time(&recv);

	uint64_t T1 = sent;
	uint64_t T2 = ntohx(rq->receive);
	uint64_t T3 = ntohx(rq->transmit);
	uint64_t T4 = recv;

	int64_t d = (T4 - T1) - (T3 - T2);
	int64_t t = (T2 - T1) + (T3 - T4);

	t /= 2;

	dump_dt("rt ", d);
	dump_dt("dt ", t);
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
	run_ntp_query(ctx);

	return 0;

}
