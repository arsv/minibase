#include <format.h>
#include <util.h>
#include <fail.h>

#include "dhcp.h"

#define endof(s) (s + sizeof(s))

static char* arg_ip(char* buf, int size, uint8_t ip[4], int mask)
{
	char* p = buf;
	char* e = buf + size - 1;

	p = fmtip(p, e, ip);

	if(mask > 0 && mask < 32) {
		p = fmtchar(p, e, '/');
		p = fmtint(p, e, mask);
	}

	*p++ = '\0';

	return buf;
}

static int maskbits(void)
{
	struct dhcpopt* opt = get_option(DHCP_NETMASK, 4);
	uint8_t* ip = (uint8_t*)opt->payload;
	int mask = 0;
	int i, b;

	if(!opt) return 0;

	for(i = 3; i >= 0; i--) {
		for(b = 0; b < 8; b++)
			if(ip[i] & (1<<b))
				break;
		mask += b;

		if(b < 8) break;
	}

	return (32 - mask);
}

static uint8_t* gateway(void)
{
	struct dhcpopt* opt = get_option(DHCP_ROUTER_IP, 4);
	return opt ? opt->payload : NULL;
}

void exec_ip4cfg(char* devname, uint8_t* ip, char** envp)
{
	char* args[10];
	char** ap = args;
	int ret;

	*ap++ = "ip4cfg";
	*ap++ = devname;

	char ips[30];
	int mask = maskbits();
	*ap++ = arg_ip(ips, sizeof(ips), ip, mask);

	char gws[20];
	uint8_t* gw = gateway();

	if(gw) {
		*ap++ = "gw";
		*ap++ = arg_ip(gws, sizeof(gws), gw, 0);
	}

	*ap++ = NULL;

	ret = execvpe(*args, args, envp);
	fail("exec", *args, ret);
}
