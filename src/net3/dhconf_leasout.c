#include <sys/file.h>
#include <sys/time.h>

#include <format.h>
#include <endian.h>
#include <util.h>

#include "dhconf.h"

static char* fmt_ip(char* p, char* e, void* ip, int len)
{
	if(len == 4)
		return fmtip(p, e, ip);
	else
		return fmtstr(p, e, "???");
}

static char* fmt_ips(char* p, char* e, void* ips, int len)
{
	int i;

	if(!len || len % 4) return p;

	for(i = 0; i < len; i += 4) {
		if(i) p = fmtstr(p, e, " ");
		p = fmtip(p, e, ips + i);
	}

	return p;
}

static char* fmt_sf(char* p, char* e, int val, char* suff)
{
	if(!val) return p;
	p = fmtstr(p, e, " ");
	p = fmtint(p, e, val);
	p = fmtstr(p, e, suff);
	return p;
}

static char* fmt_ts(char* p, char* e, uint val)
{
	p = fmti32(p, e, val);
	p = fmtstr(p, e, "s");

	if(val > 60) {
		int sec = val % 60; val /= 60;
		int min = val % 60; val /= 60;
		int hrs = val % 24; val /= 24;

		p = fmtstr(p, e, " =");
		p = fmt_sf(p, e, val, "d");
		p = fmt_sf(p, e, hrs, "h");
		p = fmt_sf(p, e, min, "m");
		p = fmt_sf(p, e, sec, "s");
	}

	return p;
}

static char* fmt_time(char* p, char* e, void* ptr, int len)
{
	if(len != 4) return p;

	uint32_t val = ntohl(*((uint32_t*)ptr));

	return fmt_ts(p, e, val);
}

static char* fmt_bytes(char* p, char* e, void* buf, int len)
{
	byte* ptr = buf;
	byte* end = buf + len;

	while(ptr < end) {
		p = fmtstr(p, e, " ");
		p = fmtbyte(p, e, *ptr++);
	}

	return p;
}

static char* fmt_vendor(char* p, char* e, void* val, int len)
{
	byte* ptr = val;
	byte* end = ptr + len;
	byte* q;
	int nontext = 0, spaced = 0;

	for(q = ptr; q < end; q++)
		if(*q < 0x20 || *q > 0x7F)
			nontext = 1;
		else if(*q == 0x20 || *q == '"' || *q == '\\')
			spaced = 1;

	if(nontext)
		return fmt_bytes(p, e, ptr, len);

	if(spaced) p = fmtchar(p, e, '"');

	for(q = ptr; q < end; q++) {
		if(*q == '"' || *q == '\\')
			p = fmtchar(p, e, '\\');
		p = fmtchar(p, e, *q);
	}

	if(spaced) p = fmtchar(p, e, '"');

	return p;
}

const struct showopt {
	int code;
	char* (*fmt)(char* p, char* e, void* val, int len);
	char* tag;
} showopts[] = {
	{  1, fmt_ip,     "netmask"    },
	{  3, fmt_ips,    "router"     },
	{  6, fmt_ips,    "dns"        },
	{ 28, fmt_ip,     "bcast"      },
	{ 42, fmt_ips,    "ntp"        },
	{ 43, fmt_vendor, "vendor"     },
	{ 51, fmt_time,   "lease-time" },
	{ 53, NULL,       "msgtype"    },
	{ 54, fmt_ip,     "server"     },
	{ 58, fmt_time,   "renew-time" },
	{ 59, fmt_time,   "rebind-in"  }
};

static const struct showopt* find_format(int code)
{
	const struct showopt* sh;

	for(sh = showopts; sh < ARRAY_END(showopts); sh++)
		if(sh->code == code)
			return sh;

	return NULL;
}

static char* fmt_option(char* p, char* e, struct dhcpopt* opt)
{
	const struct showopt* sh = find_format(opt->code);

	if(!sh) {
		p = fmtstr(p, e, "option ");
		p = fmtint(p, e, opt->code);
		p = fmt_bytes(p, e, opt->payload, opt->len);
	} else if(!sh->fmt) {
		return p;
	} else {
		p = fmtstr(p, e, sh->tag);
		p = fmtstr(p, e, " ");
		p = sh->fmt(p, e, opt->payload, opt->len);
	}

	return fmtchar(p, e, '\n');
}

void output_lease_info(CTX)
{
	if(ctx->opts & (OPT_p | OPT_q))
		return;

	FMTBUF(p, e, buf, 1024);

	p = fmtstr(p, e, "address ");
	p = fmtip(p, e, ctx->ourip);
	p = fmtchar(p, e, '\n');

	void* ptr = ctx->options;
	void* end = ptr + ctx->optlen;

	while(ptr < end) {
		struct dhcpopt* opt = ptr;

		ptr += sizeof(*opt);

		if(ptr > end) break;

		ptr += opt->len;

		if(ptr > end) break;

		p = fmt_option(p, e, opt);
	}

	writeall(STDOUT, buf, p - buf);
}
