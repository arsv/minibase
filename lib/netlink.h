#include <bits/types.h>
#include <bits/socket/netlink.h>

/* Structures common for all(?) netlink protocols except UDEV.

   None of the supported ones actually sends raw nlmsg-s,
   all of them have some sort of extra header fields.

   Pretty much any request may be replied to with a nlerr packet.
   "errno" there is a kernel return code like -EINVAL or -ENOENT.
   Value 0 means ACK of a successful request, and only gets sent
   if there was NLM_F_ACK in the request packet.

   All userspace-to-kernel packets must carry NLM_F_REQUEST flag.

   "Set-something" requests without NLM_F_ACK produce no reply
   on success and nlerr on error, messing up request-reply
   sequences. Such requests must always be sent with ACK enabled.

   "Get-something" requests return their respective structures
   on success or nlerr on error. Setting NLM_F_ACK results in
   *both* ACK and the result struct being sent on success,
   messing up request-reply sequences again. Such requests
   must always be send without ACK.

   Multipart replies have NLM_F_MULTI in each data packet,
   and those are followed by a single empty NLMSG_DONE packet. */

/* nlmsg.type */
#define NLMSG_NOOP     1
#define NLMSG_ERROR    2
#define NLMSG_DONE     3
#define NLMSG_OVERRUN  4

/* nlmsg.flags */
#define NLM_F_REQUEST        (1<<0)
#define NLM_F_MULTI          (1<<1)
#define NLM_F_ACK            (1<<2)
#define NLM_F_ECHO           (1<<3)
#define NLM_F_DUMP_INTR      (1<<4)
#define NLM_F_DUMP_FILTERED  (1<<5)

#define NLM_F_ROOT           (1<<8)
#define NLM_F_MATCH          (1<<9)
#define NLM_F_ATOMIC        (1<<10)
#define NLM_F_DUMP (NLM_F_ROOT|NLM_F_MATCH)

#define NLM_F_REPLACE        (1<<8)
#define NLM_F_EXCL           (1<<9)
#define NLM_F_CREATE        (1<<10)
#define NLM_F_APPEND        (1<<11)

struct nlmsg {
	uint32_t len;
	uint16_t type;
	uint16_t flags;
	uint32_t seq;
	uint32_t pid;
	char payload[];
} __attribute__((packed));

struct nlerr {
	struct nlmsg nlm;
	int32_t errno;
	/* original request header */
	uint32_t len;
	uint16_t type;
	uint16_t flags;
	uint32_t seq;
	uint32_t pid;
} __attribute__((packed));

struct nlattr {
	uint16_t len;
	uint16_t type;
	char payload[];
} __attribute__((packed));

/* socket.protocol = NETLINK_GENERIC,
   nlmsg.type = family-id

   Family is a service we're talking to, like "nl80211" or "batman".
   The only one with a fixed id is "ctrl", its id is GENL_ID_CTRL.
   IDs for other families must be queried by sending CTRL_CMD_GETFAMILY
   request to "ctrl" service.

   Message header (nlgen) is common for all requests.
   The actual parameters are passed as attributes.
   Attribute IDs *are* specific to each family, and so is
   expected attribute content. */

struct nlgen {
	struct nlmsg nlm;
	uint8_t cmd;
	uint8_t version;
	uint16_t unused;
	char payload[];
} __attribute__((packed));

struct nlmsg* nl_msg(void* buf, int len);
struct nlerr* nl_err(struct nlmsg* msg);
struct nlgen* nl_gen(struct nlmsg* msg);
void* nl_cast(struct nlmsg* msg, int size);

static inline int nl_len(struct nlmsg* msg) { return msg->len; }
