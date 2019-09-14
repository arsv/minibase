#include <netlink.h>
#include <netlink/pack.h>

void nc_gencmd(struct ncbuf* nc, int cmd, int ver)
{
	struct nlgen* gen;

	if(!(gen = nc_fixed(nc, sizeof(*gen))))
		return;

	gen->cmd = cmd;
	gen->version = ver;
}
