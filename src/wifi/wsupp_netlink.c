#include <sys/socket.h>
#include <sys/file.h>
#include <sys/creds.h>

#include <netlink.h>
#include <netlink/recv.h>
#include <netlink/pack.h>
#include <netlink/attr.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <printf.h>
#include <util.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

/* Netlink is used to control the card: run scans, initiate connections
   and so on. Netlink code is event-driven, and comprises of two effectively
   independent command streams, one for scanning and one for connection,
   each driving its own state machine. Most wifi cards can often scan and
   connect at the same time.

   All important NL commands have delayed effects, so we do not request ACKs
   and instead react to incoming events. */

char txbuf[512];
char rxbuf[8*1024];

int netlink; /* file descriptor */
uint nlseq;

struct nrbuf nr;
struct ncbuf nc;

void setup_netlink(void)
{
	nr_buf_set(&nr, rxbuf, sizeof(rxbuf));
	nc_buf_set(&nc, txbuf, sizeof(txbuf));
}

/* Subscribing to nl80211 only becomes possible after nl80211 kernel
   module gets loaded and initialized, which may happen after wsupp
   starts. To mend this, netlink socket is opened and initialized
   as a part of device setup. */

int open_netlink(int ifi)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC;
	int protocol = NETLINK_GENERIC;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ret;

	if(netlink >= 0)
		return -EBUSY;

	if((fd = sys_socket(domain, type, protocol)) < 0) {
		warn("socket", "NETLINK", fd);
		return fd;
	}

	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0) {
		warn("bind", "NETLINK", ret);
		sys_close(fd);
		return ret;
	}

	if((ret = init_netlink(fd, ifi)) < 0) {
		sys_close(fd);
		return ret;
	}

	netlink = fd;
	pollset = 0;

	return 0;
}

void close_netlink(void)
{
	if(netlink < 0)
		return;

	reset_auth_state();
	reset_scan_state();

	sys_close(netlink);
	nr.ptr = nr.buf;

	netlink = -1;
	pollset = 0;
	nlseq = 0;

	nr_reset(&nr);

	ifindex = 0;
}

static void genl_done(struct nlmsg* msg)
{
	if(msg->seq == scanseq)
		nlm_scan_done();
	else
		warn("stray NL done", NULL, 0);
}

static void genl_error(struct nlerr* msg)
{
	uint seq = msg->seq;
	int err = msg->errno;

	if(!err)
		return; /* stray ACK? don't need these */
	if(seq == scanseq)
		return nlm_scan_error(err);
	if(seq == authseq)
		return nlm_auth_error(err);

	/* upload_ptk or upload_gtk, no seq for those */
	abort_connection(err);
}

static const struct cmd {
	int code;
	void (*call)(struct nlgen*);
} cmds[] = {
	{ NL80211_CMD_TRIGGER_SCAN,     nlm_trigger_scan }, /* scan */
	{ NL80211_CMD_NEW_SCAN_RESULTS, nlm_scan_results },
	{ NL80211_CMD_SCAN_ABORTED,     nlm_scan_aborted },
	{ NL80211_CMD_AUTHENTICATE,     nlm_authenticate }, /* mlme */
	{ NL80211_CMD_ASSOCIATE,        nlm_associate    },
	{ NL80211_CMD_CONNECT,          nlm_connect      },
	{ NL80211_CMD_DISCONNECT,       nlm_disconnect   }
};

static void dispatch(struct nlgen* msg)
{
	const struct cmd* p;

	for(p = cmds; p < cmds + ARRAY_SIZE(cmds); p++)
		if(p->code == msg->cmd)
			return p->call(msg);
}

/* Netlink has no notion of per-device subscription.
   We will be getting notifications for all available nl80211 devices,
   not just the one we watch. */

static int match_ifi(struct nlgen* msg)
{
	int32_t* ifi;

	if(!(ifi = nl_get_i32(msg, NL80211_ATTR_IFINDEX)))
		return 0;
	if(*ifi != ifindex)
		return 0;

	return 1;
}

/* Poll reports there's something to read on the netlink fd */

void handle_netlink(void)
{
	int ret, fd = netlink;

	struct nlerr* err;
	struct nlmsg* msg;
	struct nlgen* gen;

	if((ret = nr_recv(fd, &nr)) > 0)
		;
	else if(ret == -EAGAIN)
		return;
	else if(ret < 0)
		quit("recv", "NETLINK", ret);
	else
		quit("EOF", "NETLINK", 0);

	while((msg = nr_next(&nr))) {
		if(msg->type == NLMSG_DONE)
			genl_done(msg);
		else if((err = nl_err(msg)))
			genl_error(err);
		else if(!(gen = nl_gen(msg)))
			;
		else if(!match_ifi(gen))
			;
		else dispatch(gen);
	}
}
