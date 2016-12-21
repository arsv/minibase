#include <bits/netlink.h>
#include <bits/types.h>

#define NETLINK_ADD_MEMBERSHIP 1

#define NLMSG_NOOP          0x1
#define NLMSG_ERROR         0x2
#define NLMSG_DONE          0x3
#define NLMSG_OVERRUN       0x4
#define NLMSG_MIN_TYPE     0x10

#define NLM_F_ROOT	0x100	/* specify tree	root	*/
#define NLM_F_MATCH	0x200	/* return all matching	*/
#define NLM_F_ATOMIC	0x400	/* atomic GET		*/
#define NLM_F_DUMP	(NLM_F_ROOT|NLM_F_MATCH)

struct netlink {
	int fd;
	int seq;
	int err;

	int dump;

	void* rxbuf;
	int rxlen;
	int rxend;

	void* txbuf;
	int txlen;
	int txend;
	int txover;

	int msgptr;
	int msgend;
};

struct nlmsg {
	uint32_t len;
	uint16_t type;
	uint16_t flags;
	uint32_t seq;
	uint32_t pid;
	char payload[];
} __attribute__((packed));

struct nlgen {
	uint32_t len;
	uint16_t type;
	uint16_t flags;
	uint32_t seq;
	uint32_t pid;
	uint8_t cmd;
	uint8_t version;
	uint16_t unused;
	char payload[];
} __attribute__((packed));

struct nlerr {
	uint32_t len;
	uint16_t type;
	uint16_t flags;
	uint32_t seq;
	uint32_t pid;
	int32_t errno;
	char payload[];
} __attribute__((packed));

struct nlattr {
	uint16_t len;
	uint16_t type;
	char payload[];
} __attribute__((packed));

void nl_init(struct netlink* nl);
void nl_set_txbuf(struct netlink* nl, void* buf, int len);
void nl_set_rxbuf(struct netlink* nl, void* buf, int len);
long nl_connect(struct netlink* nl, int protocol);

long nl_recv_chunk(struct netlink* nl);
long nl_send_chunk(struct netlink* nl);

void nl_new_msg(struct netlink* nl, int to);
void nl_new_cmd(struct netlink* nl, int to, uint8_t cmd, uint8_t version);
void nl_put_astr(struct netlink* nl, uint16_t type, const char* str);
void nl_put_u32(struct netlink* nl, uint16_t type, uint32_t val);
void nl_put_u64(struct netlink* nl, uint16_t type, uint64_t val);

struct nlmsg* nl_recv(struct netlink* nl);
struct nlmsg* nl_recv_seq(struct netlink* nl);
int nl_send(struct netlink* nl, int flags);
struct nlmsg* nl_send_recv(struct netlink* nl, int flags);

int nl_in_seq(struct netlink* nl, struct nlmsg* msg);
int nl_done(struct nlmsg* msg);
struct nlgen* nl_gen(struct nlmsg* msg);
struct nlerr* nl_err(struct nlmsg* msg);

int nl_msg_len(struct nlmsg* msg);
int nl_gen_len(struct nlgen* msg);
int nl_err_len(struct nlerr* msg);
int nl_attr_len(struct nlattr* at);

uint16_t* nl_u16(struct nlattr* at);
uint32_t* nl_u32(struct nlattr* at);
uint64_t* nl_u64(struct nlattr* at);
char* nl_str(struct nlattr* at);

struct nlattr* nl_get_0(struct nlgen* msg);
struct nlattr* nl_get_n(struct nlgen* msg, struct nlattr* curr);

struct nlattr* nl_get(struct nlgen* msg, int i);
char* nl_get_str(struct nlgen* msg, int i);
uint16_t* nl_get_u16(struct nlgen* msg, int i);
uint32_t* nl_get_u32(struct nlgen* msg, int i);
uint64_t* nl_get_u64(struct nlgen* msg, int i);
struct nlattr* nl_get_nest(struct nlgen* msg, int i);

struct nlattr* nl_sub_0(struct nlattr* at);
struct nlattr* nl_sub_n(struct nlattr* at, struct nlattr* curr);

struct nlattr* nl_sub(struct nlattr* at, int i);
char* nl_sub_str(struct nlattr* at, int i);
uint16_t* nl_sub_u16(struct nlattr* at, int i);
uint32_t* nl_sub_u32(struct nlattr* at, int i);
int32_t* nl_sub_i32(struct nlattr* at, int i);
uint64_t* nl_sub_u64(struct nlattr* at, int i);
struct nlattr* nl_sub_nest(struct nlattr* at, int i);
void* nl_sub_len(struct nlattr* at, int i, int len);

int nl_is_nest(struct nlattr* at);
int nl_is_str(struct nlattr* at);

void nl_dump_tx(struct netlink* nl);
void nl_dump_rx(struct netlink* nl);
void nl_hexdump(char* inbuf, int inlen);
