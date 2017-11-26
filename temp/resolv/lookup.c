#include <bits/socket/inet.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/ppoll.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <endian.h>
#include <util.h>

#include "dns.h"

ERRTAG("lookup");

struct top {
	int fd;

	byte id[2];
	byte rand[16];
	int avail;

	byte* data;
	uint size;
	uint len;
	uint ptr;

	int nscount;
	byte nsaddr[4][8];
};

#define CTX struct top* ctx __unused

static int skip_name(CTX, uint start)
{
	uint ptr = start;
	uint end = ctx->len;

	while(ptr < end) {
		uint tag = ctx->data[ptr];

		if(!tag) return ptr + 1;

		int type = (tag >> 6) & 3;

		if(type == 3) { /* reference */
			return ptr + 2;
		} else if(!type) { /* string */
			ptr += tag + 1;
		} else {
			return 0; /* reserved */
		}
	}

	return 0;
}

static void maybe_reverse(char* p, char* e)
{
	char* suff = ".in-addr.arpa";
	int slen = strlen(suff);
	char* q;
	byte rev[4], ip[4];

	if(p >= e)
		return;
	if(e - p < slen)
		return;

	char* rest = e - slen;

	if(strncmp(rest, suff, slen))
		return;
	if(!(q = parseip(p, rev)) || (q != rest))
		return;

	ip[0] = rev[3];
	ip[1] = rev[2];
	ip[2] = rev[1];
	ip[3] = rev[0];

	p = fmtip(p, e, ip);
	*p = '\0';
}

static void prep_name(CTX, uint off, char* buf, int len)
{
	char* s = buf;
	char* p = buf;
	char* e = buf + len - 1;
	uint ptr = off;
	uint end = ctx->len;

	while(ptr < end) {
		uint tag = ctx->data[ptr];

		if(!tag) break;

		int type = (tag >> 6) & 3;

		if(type == 3) {
			if(ptr + 1 >= end)
				break;
			ptr = ((tag & 0x3F) << 8) | ctx->data[ptr+1];
		} else if(!type) { /* string */
			if(ptr + 1 + tag >= end)
				break;

			if(p > s) p = fmtchar(p, e, '.');
			p = fmtraw(p, e, ctx->data + ptr + 1, tag);

			ptr += tag + 1;
		} else {
			break;
		}


	}

	*p = '\0';

	maybe_reverse(buf, p);
}

static void dump_addr(CTX, char* name, struct dnsres* dr)
{
	byte* ip = dr->data;
	int iplen = ntohs(dr->length);

	if(iplen != 4)
		return;

	tracef("%s %i.%i.%i.%i\n", name, ip[0], ip[1], ip[2], ip[3]);
}

static void dump_cname(CTX, char* name, uint doff)
{
	char buf[256];
	prep_name(ctx, doff, buf, sizeof(buf));
	tracef("%s = %s\n", name, buf);
}

static void dump_soa(CTX, char* name, uint doff)
{
	char buf[256];
	prep_name(ctx, doff, buf, sizeof(buf));
	tracef("%s :: %s\n", name, buf);
}

static void dump_ptr(CTX, char* name, uint doff)
{
	char buf[256];
	prep_name(ctx, doff, buf, sizeof(buf));
	tracef("%s <- %s\n", name, buf);
}

static void dump_resource(CTX, uint start, uint doff, struct dnsres* dr)
{
	int type = ntohs(dr->type);
	int class = ntohs(dr->class);
	char name[256];

	if(class != DNS_CLASS_IN)
		return;

	prep_name(ctx, start, name, sizeof(name));

	switch(type) {
		case DNS_TYPE_A:     return dump_addr(ctx, name, dr);
		case DNS_TYPE_CNAME: return dump_cname(ctx, name, doff);
		case DNS_TYPE_SOA:   return dump_soa(ctx, name, doff);
		case DNS_TYPE_PTR:   return dump_ptr(ctx, name, doff);
		default: tracef("unknown resource type %i\n", type);
	}
}

static int parse_resource(CTX)
{
	uint start = ctx->ptr;
	uint droff = skip_name(ctx, start);
	int drlen = sizeof(struct dnsres);

	if(droff <= start)
		return 0;
	if(ctx->len < droff + drlen)
		return 0;

	struct dnsres* dr = (struct dnsres*)(ctx->data + droff);
	uint dataoff = droff + drlen;
	uint datalen = ntohs(dr->length);
	uint nextoff = droff + drlen + datalen;

	if(nextoff > ctx->len)
		return 0;

	dump_resource(ctx, start, dataoff, dr);

	return nextoff - start;
}

static int skip_question(CTX)
{
	uint start = ctx->ptr;
	uint droff = skip_name(ctx, start);

	if(droff <= start)
		return 0;

	uint nextoff = droff + 4;

	if(nextoff > ctx->len)
		return 0;

	return nextoff - start;
}

static void parse_section(CTX, ushort ncount, int (*call)(CTX))
{
	uint i, len;
	uint count = ntohs(ncount);
	
	if(!count || !ctx->ptr)
		return;

	for(i = 0; i < count; i++) {
		if((len = call(ctx))) {
			ctx->ptr += len;
			continue;
		} else {
			ctx->ptr = 0;
			break;
		}
	}
}

static void dump_answers(CTX, struct dnshdr* dh)
{
	parse_section(ctx, dh->qdcount, skip_question);
	parse_section(ctx, dh->ancount, parse_resource);
	parse_section(ctx, dh->nscount, parse_resource);
	parse_section(ctx, dh->arcount, parse_resource);
}

static void fail_ns_error(char* name, int rc)
{
	char* msg;

	switch(rc) {
		case DNSF_RC_SERVER: msg = "server error looking for"; break;
		case DNSF_RC_FORMAT: msg = "format error looking for"; break;
		case DNSF_RC_NAME:   msg = "not found:"; break;
		case DNSF_RC_NOTIMPL: msg = "not implemented:"; break;
		case DNSF_RC_REFUSED: msg = "query refused for"; break;
		case DNSF_RC_NOTAUTH: msg = "not authorative for"; break;
		case DNSF_RC_NOTZONE: msg = "not in zone:"; break;
		default: fail("ns error", NULL, rc);
	}

	fail(msg, name, 0);
}

static char* read_whole(char* name, uint* size)
{
	int fd, ret;
	struct stat st;
	char* buf;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);
	if(st.size > 0x7FFFFFFF)
		fail(NULL, name, -E2BIG);

	buf = sys_mmap(NULL, st.size, PROT_READ, MAP_SHARED, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	*size = (uint)st.size;

	sys_close(fd);

	return buf;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static void parse_add_ns(CTX, char* p, char* e)
{
	byte ip[4];

	while(p < e && isspace(*p))
		p++;

	int len = e - p;

	if(len <= 0)
		return;

	char buf[len+1];
	memcpy(buf, p, len);
	buf[len] = '\0';

	tracef("ns %s\n", buf);

	if(!(p = parseip(buf, ip)) || (*p && !isspace(*p)))
		return;

	int c = ctx->nscount;

	memcpy(ctx->nsaddr[c], ip, 4);
	ctx->nscount++;
}

static void read_resolv_conf(CTX)
{
	uint size;
	char* buf = read_whole("/etc/resolv.conf", &size);
	char* end = buf + size;
	char* pref = "nameserver ";
	int preflen = strlen(pref);

	char *ls, *le;

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');

		if(le - ls < preflen + 1)
			continue;
		if(memcmp(ls, pref, preflen))
			continue;

		parse_add_ns(ctx, ls + preflen, le);

		if(ctx->nscount >= ARRAY_SIZE(ctx->nsaddr))
			break;
	}

	sys_munmap(buf, size);
}

static void prep_buffer(CTX, void* buf, int size)
{
	ctx->data = buf;
	ctx->size = size;
	ctx->len = 0;
}

static void prep_socket(CTX)
{
	int fd, ret;

	struct sockaddr_in self = {
		.family = AF_INET,
		.port = 0,
		.ip = { 0, 0, 0, 0 }
	};

	if((fd = sys_socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sys_bind(fd, &self, sizeof(self))) < 0)
		fail("bind", NULL, ret);

	ctx->fd = fd;
}

static void free_socket(CTX)
{
	sys_close(ctx->fd);
	ctx->fd = -1;
}

static void prep_ident(CTX)
{
	int fd, rd;
	char* name = "/dev/urandom";

	if(ctx->avail >= 2)
		goto got;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((rd = sys_read(fd, ctx->rand, sizeof(ctx->rand))) < 0)
		fail("read", name, rd);
	if(rd < 2)
		fail("cannot get enough random data", NULL, 0);

	sys_close(fd);
	ctx->avail = rd;
got:
	ctx->avail -= 2;
	memcpy(ctx->id, ctx->rand + ctx->avail, 2);
	memcpy(ctx->data, ctx->id, 2);
}

static void warnip(const char* msg, byte ip[4], int ret)
{
	FMTBUF(p, e, ipstr, 20);
	p = fmtip(p, e, ip);
	FMTEND(p, e);

	warn(msg, ipstr, ret);
}

static int send_packet(CTX, void* buf, int len, byte ip[4])
{
	int ret, fd = ctx->fd;
	struct sockaddr_in to = {
		.family = AF_INET,
		.port = htons(53),
	};

	memcpy(to.ip, ip, 4);

	if((ret = sys_sendto(fd, buf, len, 0, &to, sizeof(to))) < 0)
		warnip("send", ip, ret);

	return ret;
}

static int recv_packet(CTX, byte ip[4])
{
	int ret, fd = ctx->fd;
	struct sockaddr_in from;
	int fromlen = sizeof(from);

	byte* buf = ctx->data;
	int max = ctx->size;

	if((ret = sys_recvfrom(fd, buf, max, 0, &from, &fromlen)) < 0)
		return ret;
	if(memcmp(from.ip, ip, 4))
		return 0;

	return ret;
}

static int valid_packet(CTX)
{
	if(ctx->len < sizeof(struct dnshdr))
		return 0;
	if(memcmp(ctx->data, ctx->id, 2))
		return 0;

	return 1;
}

static int wait_readable(CTX, struct timespec* ts, byte ip[4])
{
	struct pollfd pfd = { .fd = ctx->fd, .events = POLLIN };

	int ret = sys_ppoll(&pfd, 1, ts, NULL);

	if(ret == 0)
		warnip(NULL, ip, (ret = -ETIMEDOUT));
	else if(ret < 0)
		fail("ppoll", NULL, ret);

	return ret;
}

static struct dnshdr* send_recv(CTX, byte* buf, int len, byte ip[4])
{
	int ret;
	struct timespec ts = { 2, 0 };

	prep_socket(ctx);
	prep_ident(ctx);

	if((ret = send_packet(ctx, buf, len, ip)) < 0)
		goto out;

	while(1) {
		if((ret = wait_readable(ctx, &ts, ip)) < 0)
			goto out;
		if((ret = recv_packet(ctx, ip)) != 0)
			goto out;
		if(ret && valid_packet(ctx))
			goto out;
	}
out:
	free_socket(ctx);

	if(ret < (int)sizeof(struct dnshdr))
		return NULL;

	ctx->len = ret;
	ctx->ptr = sizeof(struct dnshdr);

	return (struct dnshdr*)(ctx->data);
}

static byte* put_short(byte* ptr, int val)
{
	*ptr++ = ((val >> 8) & 0xFF);
	*ptr++ = ((val >> 0) & 0xFF);
	return ptr;
}

static void prep_request(CTX, byte* buf, int* size, char* name, ushort type)
{
	int nlen = strlen(name);
	struct dnshdr* dh = (struct dnshdr*) buf;
	byte* ptr = buf + sizeof(*dh);
	byte* end = buf + *size - 5;

	memzero(dh, sizeof(*dh));

	dh->flags = htons(DNSF_RD);
	dh->qdcount = htons(1);

	char* p = name;
	char* e = name + nlen;

	while(p < e) {
		char* q = strecbrk(p, e, '.');
		long partlen = q - p;

		if(partlen > 63)
			fail("name component too long", NULL, 0);
		if(partlen > end - ptr - 1)
			fail("name too long", NULL, partlen);

		*ptr = (byte)partlen;
		memcpy(ptr + 1, p, partlen);
		ptr += 1 + partlen;

		p = q + 1;
	}

	*ptr++ = 0x00;
	ptr = put_short(ptr, type);
	ptr = put_short(ptr, DNS_CLASS_IN);

	*size = ptr - buf;
}

static struct dnshdr* run_request(CTX, char* name, ushort type)
{
	struct dnshdr* dh;
	int namelen = strlen(name);
	int hdrlen = sizeof(*dh);
	int txlen = hdrlen+namelen+10;
	byte txbuf[txlen];
	int count = ctx->nscount;

	prep_request(ctx, txbuf, &txlen, name, type);
	
	for(int i = 0; i < count; i++) {
		byte* ip = ctx->nsaddr[i];

		if(!(dh = send_recv(ctx, txbuf, txlen, ip)))
			continue;
		
		int rc = ntohs(dh->flags) & DNSF_RC;

		if(rc == DNSF_RC_SERVER)
			continue;
		if(rc != DNSF_RC_SUCCESS)
			fail_ns_error(name, rc);

		if(i > 0) {
			memcpy(&ctx->nsaddr[0], ip, 4);
			memzero(ip, 4);
			ctx->nscount = 1;
		}

		return dh;
	}

	fail("no usable nameservers", NULL, 0);
}

static void set_nameserver(CTX, char* name)
{
	byte ip[4];
	char* p;

	if((p = parseip(name, ip)) && !*p) {
		memcpy(ctx->nsaddr[0], ip, 4);
		ctx->nscount = 1;
		return;
	}

	fail("not implemented:", __FUNCTION__, 0);
}

static void query_regular(CTX, char* name)
{
	struct dnshdr* dh;

	dh = run_request(ctx, name, DNS_TYPE_A);

	if(!(dh->ancount + dh->nscount))
		fail("server replied with no usable data", NULL, 0);

	return dump_answers(ctx, dh);
}

static void query_reverse(CTX, byte ip[4])
{
	struct dnshdr* dh;

	FMTBUF(p, e, name, 50);
	p = fmtint(p, e, ip[3]); p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[2]); p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[1]); p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[0]); p = fmtstr(p, e, ".in-addr.arpa");
	FMTEND(p, e);

	dh = run_request(ctx, name, DNS_TYPE_PTR);

	if(!dh->ancount)
		fail("server replied with no usable data", NULL, 0);

	return dump_answers(ctx, dh);
}

static void query(CTX, char* name)
{
	byte ip[4];
	char* p;

	if((p = parseip(name, ip)) && !*p)
		query_reverse(ctx, ip);
	else
		query_regular(ctx, name);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 4)
		fail("too many arguments", NULL, 0);

	char* name = argv[1];
	char rxbuf[1600];

	prep_buffer(ctx, rxbuf, sizeof(rxbuf));

	if(argc > 2)
		set_nameserver(ctx, argv[2]);
	else
		read_resolv_conf(ctx);

	query(ctx, name);

	return 0;
}
