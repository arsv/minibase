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
#include "lookup.h"

ERRTAG("lookup");

static void prep_buffer(CTX, void* buf, int size)
{
	ctx->data = buf;
	ctx->size = size;
	ctx->len = 0;
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
	char rxbuf[1600];

	prep_buffer(ctx, rxbuf, sizeof(rxbuf));

	if(argc > 2)
		set_nameserver(ctx, argv[2]);
	else
		read_resolv_conf(ctx);

	query(ctx, name);

	return 0;
}
