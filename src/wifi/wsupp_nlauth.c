#include <sys/socket.h>
#include <sys/file.h>
#include <bits/types.h>

#include <netlink.h>
#include <netlink/pack.h>
#include <netlink/attr.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <util.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

/* Wi-Fi authentication state machine. Assuming all the parameters
   have been set in struct ap, this module performs a single connection
   attempt to a single BSS.

   For most PC cards, the typical command sequence is

	<- NL80211_CMD_AUTHENTICATE      trigger_authentication
	-> NL80211_CMD_AUTHENTICATE      nlm_authenticate
	<- NL80211_CMD_ASSOCIATE         trigger_associaction
	-> NL80211_CMD_ASSOCIATE         nlm_associate
	-> NL80211_CMD_CONNECT           nlm_connect

   followed by EAPOL exchange and then normal traffic.
   Some drivers/devices merge AUTHENTICATE and ASSOCIATE into a single
   CONNECT command:

	<- NL80211_CMD_CONNECT           trigger_connect
	-> NL80211_CMD_CONNECT           nlm_connect

   There are also drivers/devices capable of doing EAPOL exchange, those
   aren't supported here at all. Those use CONNECT to carry the PSK among
   other parameters, and consequently do not need EAPOL to be triggered
   after connection.

   Which sequence should be used gets detected in init_netlink().

   Side note, sending CONNECT to a driver that need AUTH/ASSOC results in
   no errors and no connection either, just like non-connected DISCONNECT
   described below. APsel code should time us out if this happens.

   The point of the code here is mostly managing the sequences above,
   so that the AP module can just start_connection() and then just
   wait until connection_ready() or connection_end() gets called. */

/* Quick note regarding disconnects. The full sequence there is just

	<- NL80211_CMD_DISCONNECT        trigger_disconnect
	-> NL80211_CMD_DISCONNECT        nlm_disconnect

   but it only happen with user-commanded disconnects.
   Most of the time, the notification is spontaneous because disconnect
   was initiated by the device (or indirectly, by the AP):

	-> NL80211_CMD_DISCONNECT        nlm_disconnect

   Worse yet, drivers absolute love ignoring this command when there's
   no active connection:

	<- NL80211_CMD_DISCONNECT        trigger_disconnect
	   ...
	   (crickets)

   This is a big issue becase not completing AUTHENTICATE/ASSOCIATE
   sequence properly *may* leave the card in a semi-connected state,
   where it's not really connected but replies -EALREADY to incoming
   AUTHENTICATE commands.

   There is no reliable way to know whether DISCONNECT would work
   until we get CONNECT notification. Before that, it's racy.
   We handle this the same way mainline wpa_supplicant does, namely
   but not attepting to disconnect AUTHENTICATEd or ASSOCIATEd
   connections. Instead, we handle catch EALREADY from AUTHENTICATE,
   try to disconnect, and the repeat the AUTHENTICATE command again.

   This AUTH-DISCONNECT-AUTH is handled by the state machine here,
   but timed externally by the apsel code (which handles all timing
   anyway). */

/* Note there is one more AUTH-x-AUTH sequence that needs to be handled.
   AUTH may return -ENOENT in case BSS is not in the card's scan cache
   even though the AP is still in range. This is quite common actually,
   cards tend to drop scan results 10 to 20 seconds after the scan and
   we retain scans[] much longer than that.

   The solution is simple, just run a quick single-frequency scan to refresh
   the scan entry in the card's cache. However, since this sequence involves
   a scan, it is not handled here. We just call connection_ended(-ENOENT)
   and let the apsel code handle it. */

/* authstate */
#define AS_IDLE            0
#define AS_AUTHENTICATING  1
#define AS_ASSOCIATING     2
#define AS_CONNECTING      3
#define AS_CONNECTED       4
#define AS_DISCONNECTING   5
#define AS_NETDOWN         6
#define AS_EXTERNAL        7
#define AS_RETRYING        8

struct ap ap;
int authstate;
uint authseq;
int retried;

static void put_command(int cmd)
{
	int seq = nlseq++;

	authseq = seq;

	nc_header(&nc, nl80211, 0, seq);
	nc_gencmd(&nc, cmd, 0);
}

static void put_ssid_bss_attrs(void)
{
	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifindex);
	nc_put(&nc, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));
	nc_put_int(&nc, NL80211_ATTR_WIPHY_FREQ, ap.freq);
	nc_put(&nc, NL80211_ATTR_SSID, ap.ssid, ap.slen);
}

static void put_bss_auth_attrs(void)
{
	nc_put_int(&nc, NL80211_ATTR_AUTH_TYPE, 0); /* OpenSystem (?) */
}

static void put_wpa_auth_attrs(void)
{
	nc_put_flag(&nc, NL80211_ATTR_PRIVACY);
	nc_put_int(&nc, NL80211_ATTR_WPA_VERSIONS, (1<<1)); /* WPA2 */
	nc_put_int(&nc, NL80211_ATTR_CIPHER_SUITES_PAIRWISE, ap.pairwise);
	nc_put_int(&nc, NL80211_ATTR_CIPHER_SUITE_GROUP, ap.group);
	nc_put_int(&nc, NL80211_ATTR_AKM_SUITES, ap.akm);
	nc_put(&nc, NL80211_ATTR_IE, ap.txies, ap.iesize);
}

static int send_command(void)
{
	int fd = netlink;

	return nc_send(fd, &nc);
}

static int trigger_authentication(void)
{
	put_command(NL80211_CMD_AUTHENTICATE);

	put_ssid_bss_attrs();
	put_bss_auth_attrs();

	return send_command();
}

static int trigger_associaction(void)
{
	put_command(NL80211_CMD_ASSOCIATE);

	put_ssid_bss_attrs();
	put_wpa_auth_attrs();

	return send_command();
}

static int trigger_connect(void)
{
	put_command(NL80211_CMD_CONNECT);

	put_ssid_bss_attrs();
	put_bss_auth_attrs();
	put_wpa_auth_attrs();

	return send_command();
}

static int trigger_disconnect(void)
{
	put_command(NL80211_CMD_DISCONNECT);

	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifindex);

	return send_command();
}

static int trigger_connection(void)
{
	int ret;

	if(drvconnect) {
		if((ret = trigger_connect()) < 0)
			return ret;

		authstate = AS_CONNECTING;
	} else {
		if((ret = trigger_authentication()) < 0)
			return ret;

		authstate = AS_AUTHENTICATING;
	}

	return 0;
}

/* Entry point for the code here. Initiate connection to the station
   specified in struct ap. */

int start_connection(void)
{
	retried = 0;

	if(netlink <= 0)
		return -ENODEV;

	return trigger_connection();
}

/* Hard-reset and disable the auth state machine. This is not a normal
   operation, and should only happen in response to another supplicant
   trying to work on the same device. */

static void snap_to_external(void)
{
	authstate = AS_EXTERNAL;

	connection_ended(2);
}

/* The three NL confirmation messages we wait for during the connection
   sequence, at least with the cards that support AUTHENTICATE/ASSOCIATE
   commands. */

void nlm_authenticate(MSG)
{
	if(authstate == AS_IDLE)
		return;
	if(authstate != AS_AUTHENTICATING)
		return snap_to_external();

	trigger_associaction();

	authstate = AS_ASSOCIATING;
}

void nlm_associate(MSG)
{
	if(authstate == AS_IDLE)
		return;
	if(authstate != AS_ASSOCIATING)
		return snap_to_external();

	authstate = AS_CONNECTING;
}

void nlm_connect(MSG)
{
	if(authstate == AS_IDLE)
		return;
	if(authstate != AS_CONNECTING)
		snap_to_external();

	uint16_t* status = nl_get_u16(msg, NL80211_ATTR_STATUS_CODE);

	/* Ref. <linux/ieee80211.h> enum ieee80211_statuscode.
	   Zero indicates success. Error codes are small positive values. */
	if(status && *status != 0)
		return connection_ended(*status);

	authstate = AS_CONNECTED;

	connection_ready();
}

/* The card reports connection loss. This is expected after a DISCONNECT
   request, but may also happen spontaneuously for various reasons
   (AP going down, AP kicking us off, radio issues, packet timeouts etc).

   Nothing can be done here, the link is gone, just reset the state and
   ask the apsel code to decide what to do next. */

void nlm_disconnect(MSG)
{
	int err = 0;

	if(authstate == AS_IDLE) /* we did not initiate this connection */
		return;          /* do not report it */

	if(authstate == AS_RETRYING)
		if((err = trigger_connection()) >= 0)
			return;

	authstate = AS_IDLE;

	connection_ended(err);
}

/* Request disconnect, taking out current state in account.
   There is no point in sending DISCONNECT if the card is not
   expected to be in a connected state. */

int start_disconnect(void)
{
	int ret;

	if(authstate != AS_CONNECTED)
		return -ENOTCONN;
	if((ret = trigger_disconnect()) < 0)
		return ret;

	authstate = AS_DISCONNECTING;

	return 0;
}

/* Bypass the state-tracking code and inject DISCONNECT command directly.
   Only used to 'reset' the device in case it gets stuck in a semi-connected
   state. */

int force_disconnect(void)
{
	return trigger_disconnect();
}

static int retry_connect(int ret)
{
	if(ret != -EALREADY)
		return ret;
	if(retried)
		return ret;

	if(authstate == AS_AUTHENTICATING)
		;
	else if(authstate == AS_ASSOCIATING)
		;
	else if(authstate == AS_CONNECTING)
		;
	else return ret;

	if((ret = trigger_disconnect()) < 0)
		return ret;

	authstate = AS_RETRYING;
	retried = 1;

	return 0;
}

/* Netlink reports error for out last command. Typically bad news, but
   ENOENT may happen routinely for missing scan entries in the card cache.

   Note we do not handle rfkill here. If ENETDOWN happens at any point,
   we abort connection and report it up. */

void nlm_auth_error(int err)
{
	if(authstate == AS_IDLE)
		return;

	if((err = retry_connect(err)) >= 0)
		return;

	authstate = AS_IDLE;

	connection_ended(err);
}

void reset_auth_state(void)
{
	authstate = AS_IDLE;
	authseq = 0;
	retried = 0;
}
