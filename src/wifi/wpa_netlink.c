#include <sys/bind.h>
#include <sys/close.h>
#include <sys/setsockopt.h>

#include <netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <format.h>
#include <fail.h>

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

/* Device state is tracked to avoid aborts on stray notifications,
   like disconnect before connect was issued, or scan abort when we
   weren't even scanning. And also to avoid disconnecting a link
   which we did not connect. See track_state() below. */

#define NONE         0
#define SCANNING     1
#define CONNECTED    2

int devstate = NONE;

/* Ref. IEEE 802.11-2012 8.4.2.27 RSNE */

const char ies[] = {
	0x30, 0x14, /* ies { type = 48, len = 20 } */
	    0x01, 0x00, /* version 1 */
	    0x00, 0x0F, 0xAC, 0x02, /* TKIP group data chipher */
	    0x01, 0x00, /* pairwise chipher suite count */
	    0x00, 0x0F, 0xAC, 0x04, /* CCMP pairwise chipher */
	    0x01, 0x00, /* authentication and key management */
	    0x00, 0x0F, 0xAC, 0x02, /* PSK and RSNA key mgmt */
	    0x00, 0x00, /* preauth capabilities */
};

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
	return nl_ifindex(&nl, name);
}

/* The whole thing is a mix of sync and async stuff. Commands like
   AUTHENTICATE and ASSOCIATE tend to have immediate (sync) errors
   but their success is reported via notifications, and their ACKs
   are meaningless. NEW_KEY is fully synchronous. We may also get
   DISCONNECT notification pretty much at any point.

   To make the whole thing somewhat tractable, all commands are treated
   as async, with no ACK and no waiting for immediate reply, and all
   non-mainline responses (includuing ERRs) are handled in context-free
   manner very early during packet analysis. The actual code looks like

       send(ASSOCIATE)
       wait(ASSOCIATED)

   but that's only the happy path, anything else gets branched to in wait(). */

#define CONTINUE 1
#define EXPECTED 0

static int check_err(struct nlerr* msg)
{
	if(msg->nlm.seq == nl.seq)
		return msg->errno <= 0 ? msg->errno : -EBADMSG;

	return CONTINUE; /* ignore unexpected errors */
}

static int check_gen(struct nlgen* msg)
{
	uint32_t* u32;

	if(!(u32 = nl_get_u32(msg, NL80211_ATTR_IFINDEX)))
		return 0;
	if(*u32 != ifindex)
		return 0;

	return 1;
}

static void track_state(struct nlgen* msg)
{
	int cmd = msg->cmd;

	if(devstate == NONE) {
		if(cmd == NL80211_CMD_TRIGGER_SCAN)
			devstate = SCANNING;
		else if(cmd == NL80211_CMD_CONNECT)
			devstate = CONNECTED;
	} else if(devstate == SCANNING) {
		if(cmd == NL80211_CMD_SCAN_ABORTED)
			quit("scan aborted", NULL, 0);
		else if(cmd == NL80211_CMD_NEW_SCAN_RESULTS)
			devstate = NONE;
	} else if(devstate == CONNECTED) {
		if(cmd == NL80211_CMD_DISCONNECT)
			quit("disconnected", NULL, 0);
	}
}

static int check_msg(struct nlmsg* msg, int expected)
{
	struct nlerr* err;
	struct nlgen* gen;

	if((err = nl_err(msg)))
		return check_err(err);
	if(!(gen = nl_gen(msg)))
		return CONTINUE; /* non-genl message, wtf? */
	if(!check_gen(gen))
		return CONTINUE;

	track_state(gen);

	if(gen->cmd != expected)
		return CONTINUE;

	return EXPECTED;
}

static int recv_genl(int expected)
{
	int ret;
	struct nlmsg* nlm;

	do if(!(nlm = nl_recv(&nl)))
		return nl.err < 0 ? nl.err : -EBADMSG;
	while((ret = check_msg(nlm, expected)) > 0);

	return ret;
}

static int recv_check(int expected)
{
	int ret;

	if((ret = recv_genl(expected)) < 0)
		quit(lastcmd, NULL, ret);

	return ret;
}

static void send_genl(const char* cmdtag)
{
	int ret;

	if((ret = nl_send(&nl)) < 0)
		quit("nl-send", cmdtag, ret);

	lastcmd = cmdtag;
}

static void send_check(const char* cmdtag)
{
	struct nlmsg* msg = (struct nlmsg*)(nl.txbuf);

	msg->flags |= NLM_F_ACK;

	send_genl(cmdtag);

	recv_check(0);
}

/* After uploading the keys, we keep monitoring nl.fd for possible
   spontaneous disconnects. No other messages are expected by that
   point, and DISCONNECT gets handled in check_msg(), so we call it
   but ignore any returns. */

void pull_netlink(void)
{
	long ret;
	struct nlmsg* msg;

	if((ret = nl_recv_nowait(&nl)) < 0)
		quit("nl-recv", NULL, ret);

	while((msg = nl_get_nowait(&nl)))
		check_msg(msg, 0);
}

static void send_auth_request(void)
{
	int authtype = 0;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_AUTHENTICATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, bssid, sizeof(bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, frequency);
	nl_put(&nl, NL80211_ATTR_SSID, ssid, strlen(ssid));
	nl_put_u32(&nl, NL80211_ATTR_AUTH_TYPE, authtype);

	send_genl("AUTHENTICATE");
}

static void scan_group_op(int opt)
{
	int fd = nl.fd;
	int lvl = SOL_NETLINK;
	int id = scangrp;
	int ret;

	if((ret = syssetsockopt(fd, lvl, opt, &id, sizeof(id))) < 0)
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
		fail("nl-subscribe nl80211", "scan", ret);

	scan_group_op(NETLINK_ADD_MEMBERSHIP);

	send_genl("TRIGGER_SCAN");
	recv_check(NL80211_CMD_NEW_SCAN_RESULTS);

	scan_group_op(NETLINK_DROP_MEMBERSHIP);
}

/* For AUTHENTICATE command to succeed, the AP must be in the card's
   (or kernel's? probably card's) internal cache, which means the
   command must come mere seconds after a scan. Otherwise it fails
   with ENOENT.

   We cannot expect to be run right after a fresh scan, so if we
   see ENOENT here, we run a short one-frequency scan ourselves,
   to find the station we're looking for and refresh its cache entry.

   However, a scan is still a scan, it takes some time, and it may
   not be necessary if there may be fresh results in the cache.
   So we probe that first with an optimistic AUTHENTICATE request. */

void authenticate(void)
{
	int ret;
	int cmd = NL80211_CMD_AUTHENTICATE;

	send_auth_request();

	if(!(ret = recv_genl(cmd)))
		return; /* we're done */
	if(ret != -ENOENT)
		fail(lastcmd, NULL, ret);

	scan_single_freq();

	send_auth_request();
	recv_check(cmd);
}

/* The right IEs here prompt the AP to initiate EAPOL exchange. */

void associate(void)
{
	nl_new_cmd(&nl, nl80211, NL80211_CMD_ASSOCIATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, bssid, sizeof(bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, frequency);
	nl_put(&nl, NL80211_ATTR_SSID, ssid, strlen(ssid));

	/* wpa_supplicant also adds

	       NL80211_ATTR_WPA_VERSIONS
	       NL80211_ATTR_CIPHER_SUITES_PAIRWISE
	       NL80211_ATTR_CIPHER_SUITE_GROUP,
	       NL80211_ATTR_AKM_SUITES

	   Those are only needed for wext compatibility code
	   within the kernel, so we skip them. */

	nl_put(&nl, NL80211_ATTR_IE, ies, sizeof(ies));

	send_genl("ASSOCIATE");

	recv_check(NL80211_CMD_CONNECT);
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
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, 1);
	nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, tkip);
	nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, 32);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, RSC, 6);

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_MULTICAST);
	nl_end_nest(&nl, at);

	send_check("NEW_KEY GTK");
}

void disconnect(void)
{
	if(devstate != CONNECTED)
		return;

	devstate = NONE; /* disable DISCONNECT handling */

	nl_new_cmd(&nl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	send_check("DISCONNECT");
}
