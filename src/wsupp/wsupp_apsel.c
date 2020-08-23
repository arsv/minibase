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
#define OP_EXTERNAL        31

#define OP_WAIT_DISCONN    40
#define OP_WAIT_ABORT      41

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

	return ret;
}

static int connect_to_something(void)
{
	int ret;

	if((ret = pick_best_bss()) < 0)
		return ret;

	return proceed_with_connect();
}

/* ENETDOWN from any request means the device has been reset, and
   connection lost. So we should reset all our associated state. */

static void snap_to_netdown(void)
{
	close_rawsock();
	reset_eapol_state();
	reset_auth_state();
	reset_scan_state();
	clear_timer();
	clear_all_bss_marks();

	if(!ap.success)
		clear_ssid_bssid();
	if(ap.slen)
		operstate = OP_NETDOWN;
	else
		operstate = OP_STOPPED;
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
	clear_timer();

	ap.rescans = 0;

	rescan_current_bss();
}

static void snap_to_monitoring(void)
{
	force_disconnect();
	clear_ssid_bssid();

	set_timer(1*60, routine_fg_scan);

	operstate = OP_MONITORING;
}

static void unexpected_disconnect(int err)
{
	if(operstate == OP_CONNECTING) /* radio connection failed */
		goto next;
	else if(operstate == OP_EAPOL) /* EAPOL negotiations failed */
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
	if(err == -ENETDOWN) { /* device down, stop connection attempts */
		report_no_connect();
		return snap_to_netdown();
	} else if(err == -EINVAL) { /* messed up NL command somewhere */
		report_no_connect();
		return snap_to_monitoring();
	}

	clear_timer();

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

static void swait_timeout(void)
{
	force_script();
	script_exit();
}

/* This gets called from various places in case of unrecoverable
   errors. Handled here and not in nlauth solely because DISCONNECT
   has to be timed. */

void abort_connection(int err)
{
	int ret;

	aborterr = err;
	abortstate = operstate;

	operstate = OP_WAIT_ABORT;

	if((ret = sighup_script()) >= 0)
		return set_timer(2, swait_timeout);

	operstate = OP_ABORTING;

	if((ret = start_disconnect()) >= 0)
		return set_timer(1, abort_timeout);

	connection_ended(ret);
}

void script_exit(void)
{
	if(operstate == OP_WAIT_DISCONN)
		operstate = OP_DISCONNECTING;
	else if(operstate == OP_WAIT_ABORT)
		operstate = OP_ABORTING;
	else
		return;

	if(start_disconnect() >= 0)
		set_timer(1, abort_timeout);
	else
		connection_ended(-ESRCH);
}

/* EAPOL negotiations have completed, the connection is now fully usable */

void eapol_success(void)
{
	int ret;

	if(operstate != OP_EAPOL)
		return;

	operstate = OP_CONNECTED;

	ap.rescans = 0;
	ap.success = 1;
	mark_current_bss_good();

	set_timer(5*60, routine_bg_scan);

	if((ret = spawn_script()) < 0)
		abort_connection(ret);
	else
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
		return snap_to_netdown();

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
	if(operstate == OP_NETDOWN)
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

static int need_resume_first(void)
{
	int yes = 1;

	if(operstate == OP_STOPPED)
		return yes;
	if(operstate == OP_NETDOWN)
		return yes;

	return 0;
}

int set_network(byte* ssid, int slen, byte psk[32])
{
	if(slen > (int)sizeof(ap.ssid))
		return -ENAMETOOLONG;

	memzero(&ap, sizeof(ap));
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

/* User request to select network and connect.

   With current implementation, we do not support setting network without
   any APs in range, and report -ENOENT immediately. */

int ap_connect(byte* ssid, int slen, byte psk[32])
{
	int ret;

	if(!can_change_network())
		return -EISCONN;
	if(need_resume_first())
		return -ENONET;
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

	if((ret = sigint_script()) >= 0) {
		operstate = OP_WAIT_DISCONN;
		set_timer(2, swait_timeout);
		return 0;
	}
	if((ret = start_disconnect()) >= 0) {
		operstate = OP_DISCONNECTING;
		set_timer(1, abort_timeout);
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

	if(operstate == OP_NETDOWN) {
		operstate = OP_STOPPED;
	} else {
		force_disconnect();
		clear_timer();
		set_timer(10, routine_fg_scan);
		operstate = OP_MONITORING;
	}

	ret = -ECANCELED;
out:
	return ret;
}

/* User request to stop monitoring (before detaching from the device) */

int ap_detach(void)
{
	if(!can_change_network())
		return -EISCONN;

	reset_auth_state();
	reset_eapol_state();

	clear_ssid_bssid();
	reset_scan_state();
	clear_scan_table();
	clear_timer();

	operstate = OP_STOPPED;

	return 0;
}

/* User request to resume operations */

int ap_resume(void)
{
	int ret;

	if(!need_resume_first())
		return -EALREADY;

	if((ret = start_scan(0)) < 0)
		return ret;

	if(ap.success)
		operstate = OP_NET_SCAN;
	else
		operstate = OP_MONITORING;

	return 0;
}
