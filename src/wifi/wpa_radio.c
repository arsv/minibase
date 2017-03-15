#include <sys/bind.h>
#include <sys/close.h>

#include <netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <fail.h>

#include "nlfam.h"
#include "wpa.h"

/* "Radio" or netlink part deals with configuring the wireless
   card to establish low-level connection. Most of the actual
   low-level work happens either in the card's FW, or in the
   kernel, so for the most part this is just about sending
   the right NL commands. */

char txbuf[512];
char rxbuf[8192-1024];

struct netlink nl;
int nl80211;

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

static struct nlgen* recv_genl(struct netlink* nl)
{
	struct nlmsg* msg = nl_recv(nl);
	struct nlerr* err;
	struct nlgen* gen;

	if(!msg) return NULL;

	if((err = nl_err(msg))) {
		nl->err = err->errno;
		return NULL;
	}

	if(!(gen = nl_gen(msg))) {
		nl->err = EBADMSG;
		return NULL;
	}

	return gen;
}

void open_netlink(void)
{
	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));

	xchk(nl_connect(&nl, NETLINK_GENERIC, 0),
		"nl-connect", "genl");
}

void setup_netlink(void)
{
	const char* names[] = { "nl80211", "mlme", "scan", NULL };

	open_netlink();

	nl80211 = query_subscribe(&nl, names);
}

void close_netlink(void)
{
	sysclose(nl.fd);
}

int resolve_ifname(char* name)
{
	return nl_ifindex(&nl, name);
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

	xchk(nl_send(&nl), "nl-send", "NL80211_CMD_AUTHENTICATE");
}

static int wait_auth_result(void)
{
	struct nlgen* msg;

	while((msg = recv_genl(&nl)))
		if(msg->cmd == NL80211_CMD_AUTHENTICATE)
			break;
	if(!msg)
		return nl.err;

	return 0;
}

static void send_scan_request()
{
	nl_new_cmd(&nl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	struct nlattr* at = nl_put_nest(&nl, NL80211_ATTR_SCAN_FREQUENCIES);
	nl_put_u32(&nl, 0, frequency);
	nl_end_nest(&nl, at);

	if(nl_send(&nl))
		fail("TRIGGER_SCAN", NULL, nl.err);
}

static void wait_scan_results()
{
	struct nlgen* msg;

	while((msg = recv_genl(&nl)))
		if(msg->cmd == NL80211_CMD_SCAN_ABORTED)
			break;
		else if(msg->cmd == NL80211_CMD_NEW_SCAN_RESULTS)
			break;
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

	send_auth_request();

	if(!(ret = wait_auth_result()))
		return; /* we're done */
	if(ret != -ENOENT)
		fail("AUTHENTICATE", NULL, ret);

	send_scan_request();
	wait_scan_results();

	send_auth_request();

	if((ret = wait_auth_result()))
		fail("AUTHENTICATE", NULL, ret);
}

/* The right IEs here prompt the AP to initiate EAPOL exchange. */

static void send_assoc_request(void)
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

	xchk(nl_send(&nl), "nl-send", "NL80211_CMD_ASSOCIATE");
}

static void wait_assoc_connect(void)
{
	struct nlgen* msg;
	int auth = 0;

	while((msg = recv_genl(&nl)))
		if(msg->cmd == NL80211_CMD_ASSOCIATE)
			auth = 1;
		else if(msg->cmd == NL80211_CMD_CONNECT)
			break;
	if(!msg)
		fail("nl-recv", "ASSOCIATE", nl.err);
	if(!auth)
		fail("CONNECT w/o ASSOCIATE", NULL, 0);
}

void associate(void)
{
	send_assoc_request();
	wait_assoc_connect();
}

/* Packet encryption happens in the card's FW (or HW) but the keys
   are negotiated in host's userspace and must be uploaded to the card.
 
   Some cards are apparently capable of re-keying on their own if supplied
   with KCK and KEK. This feature is difficult to probe for however,
   so we don't even try. Also, if it can re-key, why doesn't it handle
   the rest of EAPOL stuff then? With a much simplier userspace tool. */

void upload_ptk(void)
{
	uint8_t seq[6] = { 0, 0, 0, 0, 0, 0 };
	uint32_t ccmp = 0x000FAC04;
	struct nlattr* at;
	int ret;

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

	if((ret = nl_send_recv_ack(&nl)))
		fail("NL80211_CMD_NEW_KEY", "PTK", ret);
}

void upload_gtk(void)
{
	uint32_t tkip = 0x000FAC02;
	struct nlattr* at;
	int ret;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, 1);
	nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, tkip);
	nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, 32);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, RSC, 6);

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_MULTICAST);
	nl_end_nest(&nl, at);

	if((ret = nl_send_recv_ack(&nl)))
		fail("NL80211_CMD_NEW_KEY", "GTK", ret);
}
