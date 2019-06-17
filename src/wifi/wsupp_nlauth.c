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

/* Wi-Fi authentication state machine.
   Typical message sequence here:

	# connect
	<- NL80211_CMD_AUTHENTICATE      trigger_authentication
	-> NL80211_CMD_AUTHENTICATE      nlm_authenticate
	<- NL80211_CMD_ASSOCIATE         trigger_associaction
	-> NL80211_CMD_ASSOCIATE         nlm_associate
	-> NL80211_CMD_CONNECT           nlm_connect

	(EAPOL exchange, then normal traffic)

	# disconnect
	<- NL80211_CMD_DISCONNECT        trigger_disconnect
	-> NL80211_CMD_DISCONNECT        nlm_disconnect

   Disconnect notifications may arrive spontaneously if initiated
   by the card (rfkill, or the AP going down), trigger_disconnect
   is only used to abort unsuccessful connection. */

struct ap ap;
int authstate;
uint authseq;

static void send_check()
{
	nl_send(&nl);
}

static void send_set_authstate(int as)
{
	send_check();

	authstate = as;
}

static void trigger_authentication(void)
{
	int authtype = 0;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_AUTHENTICATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, ap.freq);
	nl_put(&nl, NL80211_ATTR_SSID, ap.ssid, ap.slen);
	nl_put_u32(&nl, NL80211_ATTR_AUTH_TYPE, authtype);

	send_set_authstate(AS_AUTHENTICATING);
}

int start_connection(void)
{
	int ret;

	if(netlink <= 0)
		return -ENODEV;

	if(authstate != AS_IDLE)
		return -EBUSY;
	if(scanstate != SS_IDLE)
		return -EBUSY;

	if((ret = open_rawsock()) < 0)
		return ret;

	trigger_authentication();

	set_timer(1);

	return 0;
}

static void trigger_associaction(void)
{
	nl_new_cmd(&nl, nl80211, NL80211_CMD_ASSOCIATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, ap.freq);
	nl_put(&nl, NL80211_ATTR_SSID, ap.ssid, ap.slen);
	nl_put(&nl, NL80211_ATTR_IE, ap.txies, ap.iesize);

	send_set_authstate(AS_ASSOCIATING);
}

static void trigger_disconnect(void)
{
	nl_new_cmd(&nl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	send_set_authstate(AS_DISCONNECTING);
}

//static void snap_to_netdown(void)
//{
//	reset_scan_state();
//
//	if(rfkilled) {
//		authstate = AS_IDLE;
//	} else {
//		authstate = AS_NETDOWN;
//		set_timer(1);
//	}
//
//	report_net_down();
//}

/* Hard-reset and disable the auth state machine. This is not a normal
   operation, and should only happen in response to another supplicant
   trying to work on the same device. */

static void snap_to_external(char* why)
{
	authstate = AS_IDLE;

	warn("EAPOL", why, 0);

	reset_eapol_state();
	handle_external();
}

/* See comments around prime_eapol_state() / allow_eapol_sends() on why
   this stuff works the way it does. ASSOCIATE is the last command we issue
   over netlink, pretty much everything else past that point happens either
   on its own or through the rawsock. */

void nlm_authenticate(MSG)
{
	if(authstate == AS_IDLE)
		return;
	if(authstate != AS_AUTHENTICATING)
		return snap_to_external("out-of-order AUTH");

	prime_eapol_state();

	trigger_associaction();

	set_timer(1);
}

void nlm_associate(MSG)
{
	if(authstate == AS_IDLE)
		return;
	if(authstate != AS_ASSOCIATING)
		return snap_to_external("out-of-order ASSOC");

	allow_eapol_sends();

	authstate = AS_CONNECTING;
}

void nlm_connect(MSG)
{
	if(authstate == AS_IDLE)
		return;
	if(authstate != AS_CONNECTING)
		snap_to_external("out-of-order CONNECT");

	authstate = AS_CONNECTED;
}

/* start_disconnect is for user requests,
   abort_connection gets called if EAPOL negotiations fail.

   Some cards, in some cases, may silently ignore DISCONNECT request.
   There's no errors, but there's not disconnect notification either.

   Missing notification would stall the state machine here, so every
   DISCONNECT attempt gets a short timer, and if that expires we just
   assume the card is in disconnected state already. */

int start_disconnect(void)
{
	switch(authstate) {
		case AS_CONNECTING:
		case AS_CONNECTED:
			break;
		case AS_NETDOWN:
			return -ENETDOWN;
		case AS_DISCONNECTING:
			return -EBUSY;
		default:
			return -ENOTCONN;
	}

	if(netlink <= 0) /* should never really happen after the switch() */
		return -ENODEV;

	trigger_disconnect();

	set_timer(1);

	return 0;
}

void abort_connection(void)
{
	if(start_disconnect() >= 0)
		return;

	reassess_wifi_situation();
}

int force_disconnect(void)
{
	if(netlink <= 0)
		return -ENODEV;

	trigger_disconnect();

	return 0;
}

/* This gets called on DISCONNECT message below, and also
   on DISCONNECT timeout. We do not make distinction atm,
   a timed-out disconnect is assumed to be successful. */

void note_disconnect(void)
{
	reset_eapol_state();

	authstate = AS_IDLE;

	handle_disconnect();
}

void nlm_disconnect(MSG)
{
	if(authstate == AS_IDLE)
		return;

	note_disconnect();
}

/* AUTHENTICATE or ASSOCIATE timed out. Bad news. */

void note_nl_timeout(void)
{
	if(authstate == AS_AUTHENTICATING)
		warn("timed-out", "AUTHENTICATE", 0);
	else if(authstate == AS_ASSOCIATING)
		warn("timed-out", "AUTHENTICATE", 0);

	authstate = AS_IDLE;

	handle_timeout();
}

/* Netlink errors not matching scanseq are caused by AUTHENTICATE
   and related commands. These are usually various shades of EBUSY
   and EALREADY. Actual authentication (PSK check) errors originate
   from the EAPOL code and call abort_connection() directly.

   ENOENT handling is tricky. This error means that the AP is not
   in the fast card/kernel scan cache, which gets purged in like 10s
   after the scan. It does not necessary mean the AP is gone.

   If we get ENOENT, we do a fast single-frequency scan for the AP
   we're connecting to. If the AP is really gone, it will be detected
   in reconnect_to_current_ap() and not here. */

void nlm_auth_error(int err)
{
	if(authstate == AS_DISCONNECTING) {
		warn(NULL, "DISCONNECT", err);
		authstate = AS_IDLE;
		reassess_wifi_situation();
	} else if(authstate == AS_AUTHENTICATING) {
		warn(NULL, "AUTHENTICATE", err);
		authstate = AS_IDLE;
		if(err == -ENOENT)
			start_scan(ap.freq);
	} else if(authstate == AS_ASSOCIATING) {
		warn(NULL, "ASSOCIATE", err);
		if(err == -EINVAL)
			authstate = AS_IDLE;
		else
			abort_connection();
	} else if(authstate == AS_CONNECTING) {
		warn(NULL, "CONNECT", err);
		abort_connection();
	} else if(authstate == AS_CONNECTED) {
		warn("while connected:", NULL, err);
		abort_connection();
	} else {
		warn("unexpected NL error:", NULL, err);
	}
}

void reset_auth_state(void)
{
	authstate = AS_IDLE;
}

/* EAPOL code does negotiations in the user space, but the resulting
   keys must be uploaded (installed, in 802.11 terms) back to the card
   and the upload happens via netlink. */

void upload_ptk(void)
{
	uint8_t seq[6] = { 0, 0, 0, 0, 0, 0 };
	uint32_t ccmp = 0x000FAC04;
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_NEW_KEY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));

	nl_put_u8(&nl, NL80211_ATTR_KEY_IDX, 0);
	nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, ccmp);
	nl_put(&nl, NL80211_ATTR_KEY_DATA, PTK, 16);
	nl_put(&nl, NL80211_ATTR_KEY_SEQ, seq, 6);

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_UNICAST);
	nl_end_nest(&nl, at);

	send_check();
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

	if(ap.tkipgroup) {
		nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, tkip);
		nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, 32);
	} else {
		nl_put_u32(&nl, NL80211_ATTR_KEY_CIPHER, ccmp);
		nl_put(&nl, NL80211_ATTR_KEY_DATA, GTK, 16);
	}

	at = nl_put_nest(&nl, NL80211_ATTR_KEY_DEFAULT_TYPES);
	nl_put_empty(&nl, NL80211_KEY_DEFAULT_TYPE_MULTICAST);
	nl_end_nest(&nl, at);

	send_check();
}
