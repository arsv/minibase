#include <netlink.h>
#include <netlink/genl/ctrl.h>

#include <string.h>
#include <fail.h>

#include "nlfam.h"

/* GENL families (endpoints) must be resolved dynamically with
   a CTRL_CMD_GETFAMILY request to fixed "ctrl" endpoint.

   The reply also contains multicast group ids, which are dynamic
   as well. So we do a single CTRL_CMD_GETFAMILY, subscribe the
   groups and return the family id.

   This piece of code uses fail() a lot, so it's not in the lib. */

struct nlpair {
	const char* name;
	int id;
};

static void query_family(struct netlink* nl,
                         struct nlpair* fam, struct nlpair* mcast)
{
	struct nlgen* gen;
	struct nlattr* at;

	nl_new_cmd(nl, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1);
	nl_put_str(nl, CTRL_ATTR_FAMILY_NAME, fam->name);

	if(!(gen = nl_send_recv_genl(nl)))
		fail("CTRL_CMD_GETFAMILY", fam->name, nl->err);

	uint16_t* grpid = nl_get_u16(gen, CTRL_ATTR_FAMILY_ID);
	struct nlattr* groups = nl_get_nest(gen, CTRL_ATTR_MCAST_GROUPS);

	if(!grpid)
		fail("unknown nl family", fam->name, 0);
	if(!groups)
		fail("no mcast groups for", fam->name, 0);

	fam->id = *grpid;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* name = nl_sub_str(at, 1);
		uint32_t* id = nl_sub_u32(at, 2);

		if(!name || !id)
			continue;

		struct nlpair* mc;
		for(mc = mcast; mc->name; mc++)
			if(!strcmp(name, mc->name))
				mc->id = *id;
	}
}

static int count_strings(const char** names)
{
	const char** p;
	int count = 0;

	for(p = names; *p; p++)
		count++;

	return count;
}

static void fill_pairs(int n, struct nlpair* pairs, const char** names)
{
	int i;

	for(i = 0; i < n; i++) {
		pairs[i].name = names[i];
		pairs[i].id = -1;
	}

	pairs[i].name = NULL;
}

static void subscribe_groups(struct netlink* nl, struct nlpair* mcast)
{
	struct nlpair* p;

	for(p = mcast; p->name; p++)
		if(p->id >= 0)
			xchk(nl_subscribe(nl, p->id), "nl-subscribe", p->name);
		else
			fail("unknown 802.11 mcast group", p->name, 0);
}

int query_subscribe(struct netlink* nl, const char** names)
{
	int count = count_strings(names);
	struct nlpair pairs[count+1];

	fill_pairs(count, pairs, names);
	query_family(nl, &pairs[0], pairs+1);
	subscribe_groups(nl, pairs+1);

	return pairs[0].id;
}
