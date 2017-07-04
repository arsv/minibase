#include <sys/socket.h>
#include <sys/file.h>

#include <netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <fail.h>
#include <util.h>
#include <exit.h>

#include "nlfam.h"
#include "wpa.h"

/* The netlink part deals with configuring the card to establish
   low-level connection. Most of the actual low-level work happens
   either in the card's FW, or in the kernel, so for the most part
   this is just about sending the right NL commands. */

char txbuf[512];
char rxbuf[8192-1024];

struct netlink nl;
int nl80211;
int scangrp;
int netlink;

const char* lastcmd; /* for reporting only */

int connected;

/* MLME group (AUTHENTICATE, CONNECT, DISCONNECT notifications) is subscribed
   to right away, but scan (NEW_SCAN_RESULTS, SCAN_ABORTED) subscription is
   delayed until we actually need it, and may not even happen. */

void setup_netlink(void)
{
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "mlme" },
		{ -1, "scan" },
		{  0, NULL } };

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));

	xchk(nl_connect(&nl, NETLINK_GENERIC, 0),
		"nl-connect", "genl");

	if((nl80211 = query_family_grps(&nl, family, grps)) < 0)
		fail("NL family", family, nl80211);

	if(grps[0].id < 0)
		fail("NL group nl80211", grps[0].name, -ENOENT);

	xchk(nl_subscribe(&nl, grps[0].id),
		"NL subscribe nl80211", grps[0].name);

	if(grps[1].id < 0)
		fail("NL group nl80211", grps[1].name, -ENOENT);

	scangrp = grps[1].id;
	netlink = nl.fd;
}

int resolve_ifname(char* name)
{
	return getifindex(nl.fd, name);
}

/* Most of the commands here have delayed effects: the command gets ACKed
   immediately, but the requested change happens later and is announced via
   notification. E.g. send AUTHENTICATE, recv ACK, time passes, AUTHENTICATE
   notification arrives.

   For most commands, the effect notifications always arrive after the ACK,
   so wait_for() is used. The notification for DISCONNECT however arrives
   *before* the ACK, so it's pointless to wait for it and it gets handled
   as a spontaneous notification. */

static int match_ifi(struct nlgen* msg)
{
	uint32_t* ifi;

	if(!(ifi = nl_get_u32(msg, NL80211_ATTR_IFINDEX)))
		return 0;
	if(*ifi != ifindex)
		return 0;

	return 1;
}

static void check_notification(struct nlgen* msg)
{
	if(!connected)
		return;
	if(msg->cmd != NL80211_CMD_DISCONNECT)
		return;
	if(!match_ifi(msg))
		return;

	quit(NULL, NULL, 0);
}

static int send_ack(const char* tag)
{
	struct nlmsg* msg;
	struct nlerr* err;
	struct nlgen* gen;

	if((msg = nl_tx_msg(&nl)))
		msg->flags |= NLM_F_ACK;

	if(nl_send(&nl))
		quit("send", NULL, nl.err);

	while((msg = nl_recv(&nl)))
		if((err = nl_err(msg)) && (msg->seq == nl.seq))
			return err->errno;
		else if((gen = nl_gen(msg)))
			check_notification(gen);

	quit("recv", NULL, nl.err);
}

static void send_check(const char* tag)
{
	int ret;

	if((ret = send_ack(tag)) < 0)
		quit(tag, NULL, ret);
}

static int wait_for(int cmd1, int cmd2)
{
	struct nlmsg* msg;
	struct nlgen* gen;

	while((msg = nl_recv(&nl)))
		if(!(gen = nl_gen(msg)))
			continue;
		else if(!match_ifi(gen))
			continue;
		else if(gen->cmd == cmd1)
			return cmd1;
		else if(gen->cmd == cmd2 && cmd2)
			return cmd2;
		else
			check_notification(gen);

	fail("recv", NULL, nl.err);
}

/* After uploading the keys, we keep monitoring nl.fd for possible
   spontaneous disconnects. No other messages are expected by that point. */

void pull_netlink(void)
{
	long ret;
	struct nlmsg* msg;
	struct nlgen* gen;

	if((ret = nl_recv_nowait(&nl)) < 0)
		quit("nl-recv", NULL, ret);

	while((msg = nl_get_nowait(&nl)))
		if((gen = nl_gen(msg)))
			check_notification(gen);
}

void disconnect(void)
{
	nl_new_cmd(&nl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	send_check("DISCONNECT");
}

static int send_auth_request(void)
{
	int authtype = 0;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_AUTHENTICATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, bssid, sizeof(bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, frequency);
	nl_put(&nl, NL80211_ATTR_SSID, ssid, strlen(ssid));
	nl_put_u32(&nl, NL80211_ATTR_AUTH_TYPE, authtype);

	return send_ack("AUTHENTICATE");
}

static void scan_group_op(int opt)
{
	int fd = nl.fd;
	int lvl = SOL_NETLINK;
	int id = scangrp;
	int ret;

	if((ret = sys_setsockopt(fd, lvl, opt, &id, sizeof(id))) < 0)
		fail("setsockopt", "nl80211 scan", ret);
}

static void scan_single_freq()
{
	int ret;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	struct nlattr* at = nl_put_nest(&nl, NL80211_ATTR_SCAN_FREQUENCIES);
	nl_put_u32(&nl, 0, frequency);
	nl_end_nest(&nl, at);

	if((ret = nl_subscribe(&nl, scangrp)) < 0)
		fail("subscribe", "nl80211.scan", ret);

	scan_group_op(NETLINK_ADD_MEMBERSHIP);

	send_check("TRIGGER_SCAN");
	ret = wait_for(NL80211_CMD_NEW_SCAN_RESULTS, NL80211_CMD_SCAN_ABORTED);

	if(ret == NL80211_CMD_SCAN_ABORTED)
		fail("scan aborted", NULL, 0);

	scan_group_op(NETLINK_DROP_MEMBERSHIP);
}

/* For AUTHENTICATE command to succeed, the AP must be in the card's
   (or kernel's? probably card's) internal cache, which means the
   command must come mere seconds after a scan. Otherwise it fails
   with ENOENT. We cannot expect to be run right after a fresh scan,
   so if this happens, we run a short one-frequency scan ourselves.

   There's also a chance that the card retains some AP association,
   in which case we get EALREADY. This gets handled with a DISCONNECT
   request. */

void authenticate(void)
{
	int ret;

	if((ret = send_auth_request()) >= 0)
		goto wait;
	else if(ret == -ENOENT)
		goto scan;
	else if(ret == -EALREADY)
		goto conn;
	else
		goto fail;
conn:
	disconnect();

	if((ret = send_auth_request()) >= 0)
		goto wait;
	else if(ret == -ENOENT)
		goto scan;
	else
		goto fail;
scan:
	scan_single_freq();

	if((ret = send_auth_request()) < 0)
		goto fail;
wait:
	wait_for(NL80211_CMD_AUTHENTICATE, 0);
	connected = 1;
	return;
fail:
	fail("AUTHENTICATE", NULL, ret);
}

/* The right IEs here prompt the AP to initiate EAPOL exchange. */

void associate(void)
{
	nl_new_cmd(&nl, nl80211, NL80211_CMD_ASSOCIATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, bssid, sizeof(bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, frequency);
	nl_put(&nl, NL80211_ATTR_SSID, ssid, strlen(ssid));

	nl_put(&nl, NL80211_ATTR_IE, ies, iesize);

	/* wpa_supplicant also adds

	       NL80211_ATTR_WPA_VERSIONS
	       NL80211_ATTR_CIPHER_SUITES_PAIRWISE
	       NL80211_ATTR_CIPHER_SUITE_GROUP,
	       NL80211_ATTR_AKM_SUITES

	   Those are only needed for wext compatibility code
	   within the kernel, so we skip them. */

	send_check("ASSOCIATE");
	wait_for(NL80211_CMD_CONNECT, 0);
}

/* Packet encryption happens in the card's FW (or HW) but the keys
   are negotiated in host's userspace and must be uploaded to the card.

   Some cards are apparently capable of re-keying on their own if supplied
   with KCK and KEK. This feature is difficult to probe for however,
   potentially unreliable, and only needed for WoWLAN, so why bother. */

void upload_ptk(void)
{
	uint8_t seq[6] = { 0, 0, 0, 0, 0, 0 };
	uint32_t ccmp = 0x000FAC04;
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, bssid, sizeof(bssid));

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, 0);
	nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, ccmp);
	nl_put(&nl, NL80211_ATTR_KEY_DATA, PTK, 16);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, seq, 6);

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_UNICAST);
	nl_end_nest(&nl, at);

	send_check("NEW_KEY PTK");
}

void upload_gtk(void)
{
	uint32_t tkip = 0x000FAC02;
	uint32_t ccmp = 0x000FAC04;
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, gtkindex);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, RSC, 6);

	if(tkipgroup) {
		nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, tkip);
		nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, 32);
	} else {
		nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, ccmp);
		nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, 16);
	}

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_MULTICAST);
	nl_end_nest(&nl, at);

	send_check("NEW_KEY GTK");
}

/* Just fail()ing if something goes wrong is not a good idea in wpa since
   the interface will likely be left in a partially-connected state.
   Especially if the failure happens somewhere in EAPOL code, which means
   the netlink part is fully connected at the time.

   So instead this little wrapper is used to issue DISCONNECT before exiting.

   Since disconnect() itself uses common routines which may in turn quit()
   themselves, and quit() may be called from sighandler, the code should be
   guarded against double invocation. */

void quit(const char* msg, const char* arg, int err)
{
	if(connected) {
		connected = 0;
		disconnect();
	}
	if(msg || arg)
		fail(msg, arg, err);
	else
		_exit(0xFF);
}
