#include "dhconf.h"

static struct dhcpopt* get_opt(void* buf, int optlen, int code)
{
	void* ptr = buf;
	void* end = buf + optlen;

	while(ptr < end) {
		struct dhcpopt* opt = ptr;

		ptr += sizeof(*opt);
		if(ptr > end) break;
		ptr += opt->len;
		if(ptr > end) break;

		if(opt->code != code)
			continue;

		return opt;
	}

	return NULL;
}

static void* check_size(struct dhcpopt* opt, int size)
{
	if(!opt || opt->len != size)
		return NULL;

	return opt->payload;
}

void* get_msg_opt(struct dhcpmsg* msg, int optlen, int code, int size)
{
	void* options = msg->options;
	struct dhcpopt* opt = get_opt(options, optlen, code);

	return check_size(opt, size);
}

void* get_ctx_opt(CTX, int code, int size)
{
	void* options = ctx->options;
	int optlen = ctx->optlen;
	struct dhcpopt* opt = get_opt(options, optlen, code);

	return check_size(opt, size);
}

struct dhcpopt* get_ctx_option(CTX, int code)
{
	void* options = ctx->options;
	int optlen = ctx->optlen;
	
	return get_opt(options, optlen, code);
}

int get_ctx_net_prefix_bits(CTX)
{
	byte* ip;
	int mask = 0;
	int i, b;

	if(!(ip = get_ctx_opt(ctx, DHCP_NETMASK, 4)))
		return 32;

	for(i = 3; i >= 0; i--) {
		for(b = 0; b < 8; b++)
			if(ip[i] & (1<<b))
				break;
		mask += b;

		if(b < 8) break;
	}

	return (32 - mask);
}
