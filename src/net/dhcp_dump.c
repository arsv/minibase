#include <sys/file.h>
#include <sys/time.h>

#include <format.h>
#include <endian.h>
#include <util.h>

#include "dhcp.h"
#include "dhcp_udp.h"

/* Output */

struct timeval reftv;
char outbuf[1000];

static void note_reftime(void)
{
	/* Lease time is relative, but output should be an absolute
	   timestamp. Reference time is DHCPACK reception. */
	xchk(sys_gettimeofday(&reftv, NULL), "gettimeofday", NULL);
}

static char* fmt_ip(char* p, char* e, uint8_t* ip, int len)
{
	return fmtip(p, e, ip);
}

static char* fmt_ips(char* p, char* e, uint8_t* ips, int len)
{
	int i;

	if(!len || len % 4) return p;

	for(i = 0; i < len; i += 4) {
		if(i) p = fmtstr(p, e, " ");
		p = fmtip(p, e, ips + i);
	}

	return p;
}

static char* fmt_time(char* p, char* e, uint8_t* ptr, int len)
{
	if(len != 4) return p;

	uint32_t val = ntohl(*((uint32_t*)ptr));
	time_t ts = reftv.sec + val;

	p = fmti64(p, e, ts);

	return p;
}

static char* fmt_bytes(char* p, char* e, uint8_t* buf, int len)
{
	uint8_t* ptr = buf;
	uint8_t* end = buf + len;

	while(ptr < end) {
		p = fmtstr(p, e, " ");
		p = fmtbyte(p, e, *ptr++);
	}

	return p;
}

static char* fmt_vendor(char* p, char* e, uint8_t* ptr, int len)
{
	uint8_t* end = ptr + len;
	uint8_t* q;
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
	char* (*fmt)(char*, char*, uint8_t* buf, int len);
	char* tag;
} showopts[] = {
	{  1, fmt_ip,     "subnet"     },
	{  3, fmt_ips,    "router"     },
	{  6, fmt_ips,    "dns"        },
	{ 28, fmt_ip,     "bcast"      },
	{ 42, fmt_ips,    "ntp"        },
	{ 43, fmt_vendor, "vendor"     },
	{ 51, fmt_time,   "until"      },
	{ 53, NULL,       "msgtype"    },
	{ 54, fmt_ip,     "server"     },
	{ 58, fmt_time,   "renew"      },
	{ 59, fmt_time,   "rebind"     },
	{  0, NULL,       NULL         }
};

static const struct showopt* find_format(int code)
{
	const struct showopt* sh;

	for(sh = showopts; sh->code; sh++)
		if(sh->code == code)
			return sh;

	return NULL;
}

void show_config(uint8_t* ip)
{
	struct dhcpopt* opt;
	const struct showopt* sh;

	char* p = outbuf;
	char* e = outbuf + sizeof(outbuf);

	note_reftime();

	p = fmtstr(p, e, "ip ");
	p = fmt_ip(p, e, ip, 4);
	p = fmtstr(p, e, "\n");

	for(opt = first_opt(); opt; opt = next_opt(opt)) {
		sh = find_format(opt->code);

		if(sh && sh->fmt) {
			p = fmtstr(p, e, sh->tag);
			p = fmtstr(p, e, " ");
			p = sh->fmt(p, e, opt->payload, opt->len);
		} else if(sh) {
			continue;
		} else {
			p = fmtstr(p, e, "opt");
			p = fmtint(p, e, opt->code);
			p = fmt_bytes(p, e, opt->payload, opt->len);
		}
		p = fmtstr(p, e, "\n");
	}

	writeall(STDOUT, outbuf, p - outbuf);
}

void write_resolv_conf(void)
{
	struct dhcpopt* opt;
	char* name = "/var/resolv.conf";
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;
	int fd;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	if((opt = get_option(DHCP_NAME_SERVERS, 0))) {
		FMTBUF(p, e, buf, 100);
		p = fmtstr(p, e, "nameserver");
		for(int i = 0; i < opt->len - 4; i += 4) {
			p = fmtstr(p, e, " ");
			p = fmtip(p, e, opt->payload + i);
		}
		FMTENL(p, e);

		writeall(fd, buf, p - buf);
	}

	sys_close(fd);
}
