#include <string.h>

#include "wsupp.h"

/* AP selection code. This is the top-level logic: run some scans,
   initiate connection, handle disconnect, switch to other AP etc.
   Most of the work happens in other modules, this one coordinates
   things. */

/* operstate */
#define OP_STOPPED          0
#define OP_MONITORING       1
#define OP_CONNECTED        2
#define OP_SEARCHING        3

#define OP_FG_SCAN         10
#define OP_BG_SCAN         11
#define OP_NET_SCAN        12
#define OP_BSS_SCAN        13

#define OP_CONNECTING      20
#define OP_EAPOL           21
#define OP_DISCONNECTING   23
#define OP_ABORTING        24

#define OP_NETDOWN         30
#define OP_RFKILLED        31
#define OP_EXTERNAL        32

int operstate;
int rfkilled;

int aborterr;
int abortstate;

static void clear_bssid(void)
{
	ap.freq = 0;
	ap.rescans = 0;

	memzero(&ap.bssid, sizeof(ap.bssid));
}

static void clear_ssid_bssid(void)
{
	memzero(&ap, sizeof(ap));
}

static void connect_timeout(void)
{
	abort_connection(-ETIMEDOUT);
}

static int proceed_with_connect(void)
{
	int ret;

	aborterr = 0;

	if((ret = prime_eapol_state()) < 0)
		return ret;
	if((ret = open_rawsock()) < 0)
		return ret;
	if((ret = start_connection()) < 0)
		return ret;

	set_timer(2, connect_timeout);

	operstate = OP_CONNECTING;

	report_connecting();

	return ret;
}

static int connect_to_something(void)
{
	int ret;

	if((ret = pick_best_bss()) < 0)
		return ret;

	return proceed_with_connect();
}

/* RFkill interface notifies us that the kill switch has been released.
   If we were interrupted by rfkill, we should resume operations. */

void radio_restored(void)
{
	if(!rfkilled)
		return;

	rfkilled = 0;

	if(operstate != OP_RFKILLED) /* we weren't doing anything */
		return;

	ap.rescans = 0;

	if(ap.freq) /* we were tuned to a particular BSS */
		operstate = OP_BSS_SCAN; /* try to find it again */
	else if(ap.slen) /* we were searching for a particular network */
		operstate = OP_NET_SCAN; /* keep doing that */
	else /* we were just monitoring */
		operstate = OP_FG_SCAN;

	start_scan(ap.freq); /* full scan if no active BSS */
}

/* RFkill reports the card has been suppressed. */

void radio_killed(void)
{
	if(rfkilled)
		return;

	rfkilled = 1;

	if(operstate != OP_NETDOWN)
		return;

	operstate = OP_RFKILLED;

	clear_timer();
}

/* We got netdown but timed out waiting for rfkill.
   It has to be a true netdown then, not rfkill.
   Stop all operations and close the device. */

static void rfkill_timeout(void)
{
	if(operstate != OP_NETDOWN) /* probably OP_RFKILL already */
		return;

	operstate = OP_STOPPED;

	clear_ssid_bssid();
}

/* ENETDOWN from any request means the device has been reset, and
   connection lost. So we should reset all our associated state.

   Initially we stay tuned to the network. If ENETDOWN was caused by
   rfkill, we will attempt to re-connect to the same station. But if
   it was a true netdown, we will release the device.

   From our point of view, rfkill events are just stray events and
   we cannot rely on their timing. ENETDOWN, on the other hand, comes
   in response to our request, typically a scan request. So we always
   work off ENETDOWN. */

static void check_rf_netdown(void)
{
	operstate = OP_NETDOWN;

	close_rawsock();
	reset_eapol_state();
	reset_auth_state();
	reset_scan_state();
	clear_timer();

	report_no_connect();

	if(rfkilled)
		operstate = OP_RFKILLED;
	else
		set_timer(1, rfkill_timeout);
}

static void routine_fg_scan(void)
{
	int ret;

	if(operstate != OP_MONITORING)
		return;

	if((ret = start_scan(0)) >= 0)
		operstate = OP_FG_SCAN;

	set_timer(1*60, routine_fg_scan);
}

static void routine_bg_scan(void)
{
	int ret;

	if(operstate != OP_CONNECTED)
		return;

	if((ret = start_scan(0)) >= 0)
		operstate = OP_BG_SCAN;

	set_timer(5*60, routine_bg_scan);
}

/* "Searching scan" happens when we lost a good connection.
   The logic here is to keep running scan every 10s for about a minute
   it case it comes back, and then if it doesn't switch to a more relaxed
   scan rate. The goal here is to pick up rebooting APs fast. */

static void searching_scan(void)
{
	int ret;

	if(operstate != OP_SEARCHING)
		return;

	if(ap.rescans > 10)
		clear_bssid();
	else
		ap.rescans++;

	if((ret = start_scan(0)) >= 0)
		operstate = OP_NET_SCAN;

	set_timer(10, searching_scan);
}

static void expected_disconnect(void)
{
	clear_ssid_bssid();

	set_timer(5, routine_fg_scan);

	operstate = OP_MONITORING;
}

static void try_another_bss(void)
{
	int ret;

	if((ret = connect_to_something()) >= 0)
		return;

	force_disconnect();
	report_no_connect();

	if(ap.success) {
		set_timer(10, searching_scan);

		operstate = OP_SEARCHING;
	} else {
		clear_ssid_bssid();

		set_timer(1*60, routine_fg_scan);

		operstate = OP_MONITORING;
	}
}

static void reconnect_current(void)
{
	if(!current_bss_in_scans()) /* the AP is gone */
		return try_another_bss();
	if((proceed_with_connect()) < 0)
		return try_another_bss();

	/* else connection attempt is ongoing */
}

static void rescan_current_bss(void)
{
	int ret;

	operstate = OP_BSS_SCAN;
	ap.rescans++;

	if((ret = start_scan(ap.freq)) >= 0)
		return;

	operstate = OP_SEARCHING;
	set_timer(10, searching_scan);
}

/* We lost AP, rescanned the frequency and it's not there.
   Now it's time to run a full scan, maybe there are other APs
   from that network around. */

static void reassess_situation(void)
{
	int ret;

	operstate = OP_NET_SCAN;

	if((ret = start_scan(0)) < 0)
		return;

	operstate = OP_SEARCHING;
	set_timer(10, searching_scan);
}

static void lost_good_connection(void)
{
	ap.rescans = 0;
	/* mark the corresponding scanline good */
	mark_current_bss_good();

	rescan_current_bss();
}

static void snap_to_monitoring(void)
{
	force_disconnect();
	report_no_connect();
	clear_ssid_bssid();

	set_timer(1*60, routine_fg_scan);

	operstate = OP_MONITORING;
}

static void unexpected_disconnect(int err)
{
	if(operstate == OP_CONNECTED) /* radio connection failed */
		goto next;
	if(operstate == OP_EAPOL) /* EAPOL negotiations failed */
		goto next;
	else if(operstate == OP_CONNECTED)
		goto good;
	else if(operstate == OP_BG_SCAN)
		goto good;
	else return; /* we weren't connected or trying to connect? */
good:
	if(err >= 0)
		return lost_good_connection();
	/* else abnormal termination, do not re-connect */
next:
	if(err == -ENETDOWN) /* device down, stop connection attempts */
		return check_rf_netdown();
	if(err == -EINVAL) /* messed up NL command somewhere */
		return snap_to_monitoring();

	return try_another_bss();
}

/* Connection attempt may fail with -ENOENT if the BSS is not the card's
   scan cache. This is quite common, not a big deal, and not a reason to
   drop the BSS. Instead, we run a fast single-frequency scan to refresh
   the cache, and only if it's not found on rescan we try to switch BSS. */

static void try_fast_rescan(void)
{
	if(!ap.rescans)
		rescan_current_bss();
	else /* tried rescanning this bss already */
		reassess_situation();
}

/* Netlink reports AP connection has been lost or terminated. */

void connection_ended(int err)
{
	clear_timer();

	if(operstate == OP_ABORTING) {
		err = aborterr;
		operstate = abortstate;
	}

	if(operstate == OP_CONNECTING && err == -ENOENT)
		return try_fast_rescan();

	report_disconnect();

	if(operstate == OP_DISCONNECTING)
		return expected_disconnect();

	return unexpected_disconnect(err);
}

static void abort_timeout(void)
{
	reset_auth_state();

	connection_ended(-ETIMEDOUT);
}

/* This gets called from various places in case of unrecoverable
   errors. Handled here and not in nlauth solely because DISCONNECT
   has to be timed. */

void abort_connection(int err)
{
	int ret;

	aborterr = err;
	abortstate = operstate;
	operstate = OP_ABORTING;

	if((ret = start_disconnect()) < 0)
		connection_ended(ret);
	else
		set_timer(1, abort_timeout);
}

/* EAPOL negotionations have completed, the connection is now fully usable */

void eapol_success(void)
{
	if(operstate != OP_EAPOL)
		return;

	operstate = OP_CONNECTED;

	ap.rescans = 0;
	ap.success = 1;

	set_timer(5*60, routine_bg_scan);

	/* report */

	trigger_dhcp();

	report_link_ready();
}

/* Netlink reports (unencrypted) radio link has been established with the AP */

void connection_ready(void)
{
	int ret;

	if(operstate != OP_CONNECTING)
		return;

	operstate = OP_EAPOL;

	if((ret = allow_eapol_sends()) >= 0)
		return;

	abort_connection(ret);
}

/* Netlink reports end of scan. May be successful or not
   depending on err, but don't really care in most cases. */

void scan_ended(int err)
{
	report_scan_end(err);

	if(err == -ENETDOWN)
		return check_rf_netdown();

	if(operstate == OP_NET_SCAN)
		return try_another_bss();
	if(operstate == OP_BSS_SCAN)
		return reconnect_current();

	if(operstate == OP_FG_SCAN)
		operstate = OP_MONITORING;
	else if(operstate == OP_BG_SCAN)
		operstate = OP_CONNECTED;
}

static int can_change_network(void)
{
	int yes = 1;

	if(operstate == OP_STOPPED)
		return yes;
	if(operstate == OP_MONITORING)
		return yes;
	if(operstate == OP_SEARCHING)
		return yes;
	if(operstate == OP_FG_SCAN)
		return yes;
	if(operstate == OP_NET_SCAN)
		return yes;

	return 0;
}

int set_network(byte* ssid, int slen, byte psk[32])
{
	if(slen > (int)sizeof(ap.ssid))
		return -ENAMETOOLONG;

	memcpy(ap.ssid, ssid, slen);
	ap.slen = slen;

	clear_bssid();
	clear_all_bss_marks();

	memcpy(PSK, psk, 32);

	return 0;
}

int time_to_scan(void)
{
	if(operstate == OP_MONITORING)
		;
	else if(operstate == OP_SEARCHING)
		;
	else if(operstate == OP_CONNECTED)
		;
	else return -1;

	return get_timer();
}

/* User request to start monitoring (typically after setting up a device) */

int ap_monitor(void)
{
	int ret;

	if(operstate == OP_EXTERNAL)
		operstate = OP_STOPPED;
	if(operstate != OP_STOPPED)
		return -EBUSY;

	if((ret = start_scan(0)) < 0)
		return ret;

	operstate = OP_FG_SCAN;
	set_timer(1*60, routine_fg_scan);

	return 0;
}

/* User request to select network and connect.

   With current implementation, we do not support setting network without
   any APs in range, and report -ENOENT immediately. */

int ap_connect(byte* ssid, int slen, byte psk[32])
{
	int ret;

	if(!can_change_network())
		return -EISCONN;
	if((ret = set_network(ssid, slen, psk)) < 0)
		return ret;
	if((ret = connect_to_something()) >= 0)
		return ret;

	clear_ssid_bssid();

	return ret;
}

/* User request to disconnect and drop current network */

int ap_disconnect(void)
{
	int ret;

	if((ret = start_disconnect()) >= 0) {
		set_timer(1, abort_timeout);
		operstate = OP_DISCONNECTING;
		return 0;
	}

	clear_ssid_bssid();
	reset_auth_state();
	reset_eapol_state();

	if(operstate == OP_STOPPED)
		goto out;
	if(operstate == OP_MONITORING)
		goto out;
	if(operstate == OP_FG_SCAN)
		goto out;

	clear_timer();
	force_disconnect();
	set_timer(10, routine_fg_scan);

	operstate = OP_MONITORING;
out:
	return ret;
}

/* User request to stop monitoring (before detaching from the device) */

static void clear_ap_state(void)
{
	reset_auth_state();
	reset_eapol_state();

	clear_ssid_bssid();
	reset_scan_state();
	clear_scan_table();
	clear_timer();

	operstate = OP_STOPPED;
}

int ap_detach(void)
{
	if(!can_change_network())
		return -EISCONN;

	clear_ap_state();

	return 0;
}

int ap_reset(void)
{
	clear_ap_state();

	return ap_monitor();
}
