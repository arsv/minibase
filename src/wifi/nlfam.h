struct netlink;

int query_family(struct netlink* nl, const char* name);
int query_subscribe(struct netlink* nl, const char** names);
