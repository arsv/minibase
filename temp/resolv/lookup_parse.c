#include <string.h>
#include <format.h>
#include <endian.h>
#include <printf.h>
#include <util.h>

#include "dns.h"
#include "lookup.h"

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

static void use_nameserver(CTX, uint start, uint doff, struct dnsres* dr)
{
	int type = ntohs(dr->type);
	int class = ntohs(dr->class);
	int drlen = ntohs(dr->length);

	if(class != DNS_CLASS_IN)
		return;
	if(type != DNS_TYPE_A)
		return;
	if(drlen != 4)
		return;
	if(ctx->nscount >= ARRAY_SIZE(ctx->nsaddr))
		return;

	int c = ctx->nscount;
	memcpy(ctx->nsaddr[c++], dr->data, 4);
	ctx->nscount = c;
}

static int parse_nsaddrs(CTX)
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

	use_nameserver(ctx, start, dataoff, dr);

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

void dump_answers(CTX, struct dnshdr* dh)
{
	parse_section(ctx, dh->qdcount, skip_question);
	parse_section(ctx, dh->ancount, parse_resource);
	parse_section(ctx, dh->nscount, parse_resource);
	parse_section(ctx, dh->arcount, parse_resource);
}

void fill_nsaddrs(CTX, struct dnshdr* dh)
{
	parse_section(ctx, dh->qdcount, skip_question);
	parse_section(ctx, dh->ancount, parse_nsaddrs);
}
