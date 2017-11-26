#include <bits/socket/inet.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ppoll.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <endian.h>
#include <util.h>

#include "dns.h"
#include "lookup.h"

ERRTAG("lookup");

char dnsbuf[2048];
char outbuf[2048];

static void prep_buffers(CTX)
{
	ctx->data = (byte*)dnsbuf;
	ctx->size = sizeof(dnsbuf);
	ctx->len = 0;
	ctx->ptr = 0;

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = outbuf;
	ctx->bo.len = sizeof(outbuf);
	ctx->bo.ptr = 0;
}

static uint read_whole(char* name, char* buf, uint max)
{
	int fd, ret;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	if((ret = sys_read(fd, buf, max)) < 0)
		fail("read", name, ret);
	if(ret >= max)
		fail(NULL, name, -E2BIG);

	sys_close(fd);

	return ret;
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

	if(!(p = parseip(buf, ip)) || (*p && !isspace(*p)))
		return;

	int c = ctx->nscount;

	memcpy(ctx->nsaddr[c], ip, 4);
	ctx->nscount++;
}

static void read_resolv_conf(CTX)
{
	char* pref = "nameserver ";
	int preflen = strlen(pref);

	char* buf = outbuf;
	uint max = sizeof(outbuf);
	uint size = read_whole("/etc/resolv.conf", buf, max);
	char* end = buf + size;

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
}

static void resolve_ns_name(CTX, char* name)
{
	struct dnshdr* dh;

	dh = run_request(ctx, name, DNS_TYPE_A);

	if(!(dh->ancount))
		fail("cannot resolve", name, 0);

	ctx->nscount = 0;

	return fill_nsaddrs(ctx, dh);
}

static void set_nameserver(CTX, char* name)
{
	byte ip[4];
	char* p;

	if((p = parseip(name, ip)) && !*p) {
		memcpy(ctx->nsaddr[0], ip, 4);
		ctx->nscount = 1;
	} else {
		read_resolv_conf(ctx);
		resolve_ns_name(ctx, name);
	}
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

	prep_buffers(ctx);

	if(argc > 2)
		set_nameserver(ctx, argv[2]);
	else
		read_resolv_conf(ctx);

	query(ctx, name);

	bufoutflush(&ctx->bo);

	return 0;
}
