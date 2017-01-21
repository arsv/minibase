/* Debug only, should not be used in any finished code. */

struct nlmsg;
struct nlerr;
struct nlgen;

struct ifinfomsg;
struct ifaddrmsg;

void nl_dump_msg(struct nlmsg* msg);
void nl_dump_gen(struct nlgen* msg);
void nl_dump_err(struct nlerr* msg);

void nl_dump_ifinfo(struct ifaddrmsg* msg);
void nl_dump_ifaddr(struct ifaddrmsg* msg);

void nl_dump_attrs_in(char* buf, int len);
void nl_hexdump(char* inbuf, int inlen);
