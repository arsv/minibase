#include <bits/errno.h>

#include <netlink.h>
#include <netlink/genl/ctrl.h>

#include <string.h>

#include "nlfam.h"

/* GENL families (endpoints) must be resolved dynamically with
   a CTRL_CMD_GETFAMILY request to fixed "ctrl" endpoint.

   The reply also contains multicast group ids, which are dynamic
   as well. So we do a single CTRL_CMD_GETFAMILY, subscribe the
   groups and return the family id. */

int query_family_grps(struct netlink* nl, const char* name, struct nlpair* grps)
{
	struct nlgen* gen;
	struct nlattr* at;

	nl_new_cmd(nl, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1);
	nl_put_str(nl, CTRL_ATTR_FAMILY_NAME, name);

	if(!(gen = nl_send_recv_genl(nl)))
		return -ENOENT;

	uint16_t* grpid = nl_get_u16(gen, CTRL_ATTR_FAMILY_ID);
	struct nlattr* groups = nl_get_nest(gen, CTRL_ATTR_MCAST_GROUPS);

	if(!grpid)
		return -EBADMSG;
	if(!groups)
		goto out;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* name = nl_sub_str(at, 1);
		uint32_t* id = nl_sub_u32(at, 2);

		if(!name || !id)
			continue;

		struct nlpair* mc;
		for(mc = grps; mc->name; mc++)
			if(!strcmp(name, mc->name))
				mc->id = *id;
	}
out:
	return *grpid;
}
