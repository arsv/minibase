#include <sys/gettimeofday.h>

#include <format.h>
#include <endian.h>
#include <util.h>
#include <fail.h>

#include "dhcp.h"
#include "dhcp_udp.h"

/* Output */

struct timeval reftv;
char outbuf[1000];

static void note_reftime(void)
{
	/* Lease time is relative, but output should be an absolute
	   timestamp. Reference time is DHCPACK reception. */
	xchk(sysgettimeofday(&reftv, NULL), "gettimeofday", NULL);
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
	time_t ts = reftv.tv_sec + val;

	p = fmti64(p, e, ts);

	return p;
}

const struct showopt {
	int key;
	char* (*fmt)(char*, char*, uint8_t* buf, int len);
	char* tag;
} showopts[] = {
	{  1, fmt_ip,  "subnet" },
	{  3, fmt_ips, "router" },
	{ 54, fmt_ip,  "server" },
	{ 51, fmt_time, "until" },
	{  6, fmt_ips, "dns" },
	{ 42, fmt_ips, "ntp" },
	{  0, NULL, NULL }
};

void show_config(uint8_t* ip)
{
	char* p = outbuf;
	char* e = outbuf + sizeof(outbuf);

	note_reftime();

	p = fmtstr(p, e, "ip ");
	p = fmt_ip(p, e, ip, 4);
	p = fmtstr(p, e, "\n");

	const struct showopt* sh;
	struct dhcpopt* opt;

	for(sh = showopts; sh->key; sh++) {
		if(!(opt = get_option(sh->key, 0)))
			continue;
		p = fmtstr(p, e, sh->tag);
		p = fmtstr(p, e, " ");
		p = sh->fmt(p, e, opt->payload, opt->len);
		p = fmtstr(p, e, "\n");
	};

	writeall(STDOUT, outbuf, p - outbuf);
}
