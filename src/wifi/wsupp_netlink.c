#include <sys/socket.h>
#include <sys/file.h>

#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>
#include <netlink/dump.h>

#include <string.h>
#include <util.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

/* Netlink is used to control the card: run scans, initiate connections
   and so on. Netlink code is event-driven, and comprises of two effectively
   independent command streams, one for scanning and one for connection,
   each driving its own state machine. Most wifi cards can often scan and
   connect at the same time.

   Most NL commands have delayed effects, and any (non-scan) errors are
   handled exactly the same way, so we do not request ACKs. */

char txbuf[512];
char rxbuf[8*1024];

struct netlink nl;
int netlink; /* = nl.fd */

/* Subscribing to nl80211 only becomes possible after nl80211 kernel
   module gets loaded and initialized, which may happen after wsupp
   starts. To mend this, netlink socket is opened and initialized
   as a part of device setup. */

int open_netlink(int ifi)
{
	int ret;

	if(netlink >= 0)
		return 0;

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));

	if((ret = nl_connect(&nl, NETLINK_GENERIC, 0)) < 0) {
		warn("nl-connect", NULL, ret);
		return ret;
	}
	if((ret = init_netlink(ifi)) < 0) {
		sys_close(nl.fd);
		return ret;
	}

	netlink = nl.fd;
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
	memzero(&nl, sizeof(nl));
	netlink = -1;
	pollset = 0;
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
	if(msg->nlm.seq == scanseq)
		nlm_scan_error(msg->errno);
	else if(msg->nlm.seq == authseq)
		nlm_auth_error(msg->errno);
	else
		warn("stray NL error", NULL, msg->errno);
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

void handle_netlink(void)
{
	int ret;

	struct nlerr* err;
	struct nlmsg* msg;
	struct nlgen* gen;

	if((ret = nl_recv_nowait(&nl)) < 0)
		quit("nl-recv", NULL, ret);

	while((msg = nl_get_nowait(&nl)))
		if(msg->type == NLMSG_DONE)
			genl_done(msg);
		else if((err = nl_err(msg)))
			genl_error(err);
		else if(!(gen = nl_gen(msg)))
			;
		else if(!match_ifi(gen))
			;
		else dispatch(gen);

	nl_shift_rxbuf(&nl);
}
