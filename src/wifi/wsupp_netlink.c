#include <sys/socket.h>
#include <sys/file.h>

#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>
#include <netlink/dump.h>

#include <string.h>
#include <util.h>

#include "wsupp.h"

/* Netlink is used to control the card: run scans, initiate connections
   and so on. Netlink code is event-driven, and comprises of two state
   machines, one for scanning and one for connection. The two are effectively
   independent, as wifi cards can often scan and connect at the same time.

   Most NL commands have delayed effects, and any (non-scan) errors are
   handled exactly the same way, so we do not request ACKs.
   Normal command sequences look like this:

	# connect
	<- NL80211_CMD_AUTHENTICATE      trigger_authentication
	-> NL80211_CMD_AUTHENTICATE      nlm_authenticate
	<- NL80211_CMD_ASSOCIATE         trigger_associaction
	-> NL80211_CMD_ASSOCIATE         nlm_associate
	-> NL80211_CMD_CONNECT           nlm_connect

	# disconnect
	<- NL80211_CMD_DISCONNECT        trigger_disconnect
	-> NL80211_CMD_DISCONNECT        nlm_disconnect

	# scan
	<- NL80211_CMD_TRIGGER_SCAN      start_scan
	-> NL80211_CMD_TRIGGER_SCAN      nlm_trigger_scan
	-> NL80211_CMD_NEW_SCAN_RESULTS  nlm_scan_results
	<- NL80211_CMD_GET_SCAN          trigger_scan_dump
	-> NL80211_CMD_NEW_SCAN_RESULTS* nlm_scan_results
	-> NL80211_CMD_NEW_SCAN_RESULTS* nlm_scan_results
	...
	-> NL80211_CMD_NEW_SCAN_RESULTS* nlm_scan_results
	-> NLMSG_DONE                    genl_done

   Disconnect notifications may arrive spontaneously if initiated
   by the card (rfkill, or the AP going down), trigger_disconnect
   is only used to abort unsuccessful connection. */

#define SR_SCANNING_ONE_FREQ (1<<0)
#define SR_RECONNECT_CURRENT (1<<1)
#define SR_CONNECT_SOMETHING (1<<2)

char txbuf[512];
char rxbuf[8*1024];

struct netlink nl;
int netlink;
static int nl80211;
static int scanreq;
static uint scanseq;

int authstate;
int scanstate;

struct ap ap;

#define MSG struct nlgen* msg __unused

/* When aborting for whatever reason, terminate the connection.
   Not doing so may leave the card in a (partially-)connected state.
   It's not that much of a problem but may be confusing.

   No point in waiting for notifications if the goal is to reset
   the device and quit. Unlike regular trigger_disconnect, this does
   a synchronous ACK-ed command. */

void quit(const char* msg, char* arg, int err)
{
	if(msg || arg || err)
		warn(msg, arg, err);

	if(authstate == AS_IDLE)
		goto out;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_send_recv_ack(&nl);
out:
	unlink_control();
	_exit(0xFF);
}

static int nle(const char* msg, const char* arg, int err)
{
	sys_close(nl.fd);
	memzero(&nl, sizeof(nl));
	warn(msg, arg, err);
	pollset = 0;
	return err;
}

/* Socket-level errors on netlink socket should not happen. */

static void send_check(void)
{
	int ret;

	if((ret = nl_send(&nl)) >= 0)
		return;

	quit("nl-send", NULL, ret);
}

static void send_set_authstate(int as)
{
	send_check();

	authstate = as;
}

static void reset_scan_state(void)
{
	scanstate = SS_IDLE;
	scanseq = 0;
	scanreq = 0;
}

/* Subscribing to nl80211 only becomes possible after nl80211 kernel
   module gets loaded and initialized, which may happen after wsupp
   starts. To mend this, netlink socket is opened and initialized
   as a part of device setup. */

int open_netlink(void)
{
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "mlme" },
		{ -1, "scan" },
		{  0, NULL } };
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

	if((ret = query_family_grps(&nl, family, grps)) < 0)
		return nle("NL family", family, ret);
	if(grps[0].id < 0)
		return nle("NL group nl80211", grps[0].name, -ENOENT);
	if(grps[1].id < 0)
		return nle("NL group nl80211", grps[1].name, -ENOENT);

	nl80211 = ret;

	if((ret = nl_subscribe(&nl, grps[0].id)) < 0)
		return nle("NL subscribe nl80211", grps[0].name, ret);
	if((ret = nl_subscribe(&nl, grps[1].id)) < 0)
		return nle("NL subscribe nl80211", grps[1].name, ret);

	netlink = nl.fd;
	pollset = 0;

	return 0;
}

void close_netlink(void)
{
	if(netlink < 0)
		return;

	reset_scan_state();

	sys_close(netlink);
	memzero(&nl, sizeof(nl));
	netlink = -1;
	pollset = 0;
}

/* The weird logic below handles the cases when a re-scan or a routine
   scheduled scan coincides with a user-requested full range scan. */

int start_scan(int freq)
{
	struct nlattr* at;
	int ret;

	if(netlink <= 0)
		return -ENODEV;

	if(scanstate == SS_IDLE) {
		/* no ongoing scan, great */
		if(freq > 0)
			scanreq |= SR_RECONNECT_CURRENT;
		if(freq < 0)
			scanreq |= SR_CONNECT_SOMETHING;
	} else if(scanreq & SR_SCANNING_ONE_FREQ) {
		/* ongoing single-freq scan, bad */
		return -EBUSY;
	} else { /* ongoing whole-range scan */
		if(freq > 0)
			scanreq |= SR_RECONNECT_CURRENT;
		if(freq < 0)
			scanreq |= SR_CONNECT_SOMETHING;
		return 0;
	}

	nl_new_cmd(&nl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	if(freq > 0) {
		scanreq |= SR_SCANNING_ONE_FREQ | SR_RECONNECT_CURRENT;
		at = nl_put_nest(&nl, NL80211_ATTR_SCAN_FREQUENCIES);
		nl_put_u32(&nl, 0, freq); /* FREQUENCIES[0] = freq */
		nl_end_nest(&nl, at);
	}

	if((ret = nl_send(&nl)) < 0) {
		scanreq = 0;
		return ret;
	}

	scanstate = SS_SCANNING;
	scanseq = nl.seq;

	return 0;
}

int start_void_scan(void)
{
	return start_scan(0);
}

int start_full_scan(void)
{
	return start_scan(-1);
}

static void mark_stale_scan_slots(struct nlgen* msg)
{
	struct nlattr* at;
	struct nlattr* sb;
	int32_t* fq;
	struct scan* sc;

	if(!(at = nl_get_nest(msg, NL80211_ATTR_SCAN_FREQUENCIES)))
		return;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		for(sb = nl_sub_0(at); sb; sb = nl_sub_n(at, sb))
			if(!(fq = (int*)nl_u32(sb)))
				continue;
			else if(*fq == sc->freq)
				break;
		if(sb) sc->flags |= SF_STALE;
	}
}

static void reset_ies_data(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		sc->ies = NULL;
		sc->ieslen = 0;
		sc->flags &= ~(SF_SEEN | SF_GOOD | SF_TKIP);
	}

	hp.ptr = hp.org; /* heap reset */
}

/* Note we cannot replace IEs partially atm, only all at once.
   Single-freqnecy scan *appends* new IEs block for the AP,
   leaving the old one in place. Subsequent full-range scan
   should clean up the old entries.

   Doing some proper memory management with IEs might have been
   a better idea but the amount of code and time spend hardly
   justifies the negligible effect. */

static void trigger_scan_dump(void)
{
	int ret;

	if(!(scanreq & SR_SCANNING_ONE_FREQ))
		reset_ies_data();

	nl_new_cmd(&nl, nl80211, NL80211_CMD_GET_SCAN, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	if((ret = nl_send_dump(&nl)) < 0) {
		warn("nl-send", "scan dump", ret);
		reset_scan_state();
	} else {
		scanstate = SS_SCANDUMP;
		scanseq = nl.seq;
	}
}

static int get_i32_or_zero(struct nlattr* bss, int key)
{
	int32_t* val = nl_sub_i32(bss, key);
	return val ? *val : 0;
}

static void parse_scan_result(struct nlgen* msg)
{
	struct scan* sc;
	struct nlattr* bss;
	struct nlattr* ies;
	byte* bssid;
	byte* stored;

	if(!(bss = nl_get_nest(msg, NL80211_ATTR_BSS)))
		return;
	if(!(bssid = nl_sub_of_len(bss, NL80211_BSS_BSSID, 6)))
		return;
	if(!(sc = grab_scan_slot(bssid)))
		return; /* out of scan slots */

	memcpy(sc->bssid, bssid, 6);
	sc->freq = get_i32_or_zero(bss, NL80211_BSS_FREQUENCY);
	sc->signal = get_i32_or_zero(bss, NL80211_BSS_SIGNAL_MBM);
	sc->flags &= ~SF_STALE;

	if(!(ies = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		return;
	if(!(stored = heap_store(nl_payload(ies), nl_paylen(ies))))
		return;

	sc->ies = stored;
	sc->ieslen = nl_paylen(ies);
}

/* NL80211_CMD_TRIGGER_SCAN arrives with a list of frequencies being
   scanned. We use it to mark and later remove stale scan entries.
   The list may not cover the whole range, e.g. if it's a single-freq
   scans, so the point is to mark only the entries being re-scanned. */

static void nlm_trigger_scan(MSG)
{
	if(scanstate != SS_SCANNING)
		return;

	mark_stale_scan_slots(msg);

	report_scanning();
}

/* Non-MULTI scan results command means the card is done scanning,
   and it comes empty. We then send a dump request, which results
   in a bunch of messages with the same NL80211_CMD_NEW_SCAN_RESULTS code
   but also with the MULTI flag set. The dump ends with a NLMSG_DONE. */

static void nlm_scan_results(MSG)
{
	if(msg->nlm.flags & NLM_F_MULTI)
		parse_scan_result(msg);
	else if(scanstate == SS_SCANNING)
		trigger_scan_dump();
}

static void nlm_scan_aborted(MSG)
{
	if(scanstate != SS_SCANNING)
		return;

	warn("scan aborted", NULL, 0);
	report_scan_fail();

	reset_scan_state();
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

/* Hard-reset and disable the auth state machine. This is not a normal
   operation, and should only happen in response to another supplicant
   trying to work on the same device. */

static void snap_to_disabled(char* why)
{
	opermode = OP_DETACH;
	authstate = AS_EXTERNAL;

	warn("EAPOL", why, 0);

	reset_eapol_state();
	handle_disconnect();
}

/* See comments around prime_eapol_state() / allow_eapol_sends() on why
   this stuff works the way it does. ASSOCIATE is the last command we issue
   over netlink, pretty much everything else past that point happens either
   on its own or through the rawsock. */

static void nlm_authenticate(MSG)
{
	if(authstate == AS_EXTERNAL)
		return;
	if(authstate != AS_AUTHENTICATING)
		return snap_to_disabled("out-of-order AUTH");

	prime_eapol_state();

	trigger_associaction();
}

static void nlm_associate(MSG)
{
	if(authstate == AS_EXTERNAL)
		return;
	if(authstate != AS_ASSOCIATING)
		return snap_to_disabled("out-of-order ASSOC");

	allow_eapol_sends();

	authstate = AS_CONNECTING;
}

static void nlm_connect(MSG)
{
	if(authstate == AS_EXTERNAL)
		return;
	if(authstate != AS_CONNECTING)
		snap_to_disabled("out-of-order CONNECT");

	authstate = AS_CONNECTED;
}

/* start_disconnect is for user requests,
   abort_connection gets called if EAPOL negotiations fail. */

int start_disconnect(void)
{
	if(netlink <= 0)
		return -ENOTCONN;
	/* not ENODEV here ^ to prevent the client from
	   trying to pick a device and retry the request */

	switch(authstate) {
		case AS_IDLE:
			return -ENOTCONN;
		case AS_NETDOWN:
			return -ENETDOWN;
		case AS_DISCONNECTING:
			return -EBUSY;
	}

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

/* Some cards, in some cases, may silently ignore DISCONNECT request.
   There's no errors, but there's not disconnect notification either.

   Missing notification would stall the state machine here, so every
   DISCONNECT attempt gets a short timer, and if that expires we just
   assume the card is in disconnected state already. */

void note_disconnect(void)
{
	reset_eapol_state();

	authstate = AS_IDLE;

	handle_disconnect();
}

static void nlm_disconnect(MSG)
{
	if(authstate == AS_IDLE)
		return;

	note_disconnect();
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

/* NLMSG_DONE indicates the end of a dump. The only kind of dumps
   that happens in wsupp is scan dump.

   A pending single-frequency scan request (freqreq) means we're
   either pre-scanning an AP after ENOENT, or re-scanning it after
   losing a connection. In both cases the configured AP should be
   tried first before proceeding to reassess_wifi_situation(). */

static void drop_stale_scan_slots(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(sc->flags & SF_STALE)
			free_scan_slot(sc);
}

static void genl_done(struct nlmsg* msg)
{
	int current = scanreq;

	if(scanstate != SS_SCANDUMP)
		return;
	if(msg->seq != scanseq)
		return;

	reset_scan_state();

	drop_stale_scan_slots();
	maybe_trim_heap();

	report_scan_done();

	if(current & SR_RECONNECT_CURRENT)
		return reconnect_to_current_ap();
	if(current & SR_CONNECT_SOMETHING)
		return reassess_wifi_situation();
}

/* Netlink errors caused by scan-related commands; we track those
   with scanseq. There's really not that much to do here, if a scan
   fails (and it's not ENETDOWN which gets handled separately). */

static void handle_scan_error(int err)
{
	if(!err) return; /* stray ACK */

	reset_scan_state();
	report_scan_fail();
}

static void snap_to_netdown(void)
{
	reset_scan_state();

	if(rfkilled) {
		authstate = AS_IDLE;
	} else {
		authstate = AS_NETDOWN;
		set_timer(1);
	}

	report_net_down();
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

static void handle_auth_error(int err)
{
	if(authstate == AS_DISCONNECTING) {
		authstate = AS_IDLE;
		reassess_wifi_situation();
	} else if(authstate == AS_AUTHENTICATING) {
		authstate = AS_IDLE;
		if(err == -ENOENT)
			start_scan(ap.freq);
	} else if(authstate == AS_ASSOCIATING) {
		if(err == -EINVAL)
			authstate = AS_IDLE;
		else
			abort_connection();
	} else if(authstate == AS_CONNECTING) {
		abort_connection();
	} else if(authstate == AS_CONNECTED) {
		abort_connection();
	}
}

static void genl_error(struct nlerr* msg)
{
	if(msg->errno == -ENETDOWN)
		snap_to_netdown();
	else if(msg->nlm.seq == scanseq)
		handle_scan_error(msg->errno);
	else if(authstate != AS_IDLE)
		handle_auth_error(msg->errno);
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
