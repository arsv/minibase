struct netlink;

struct nlpair {
	int id;
	const char* name;
};

int query_family(struct netlink* nl, const char* name);
int query_family_grps(struct netlink* nl, const char* fam, struct nlpair* grps);
