#include <sys/socket.h>
#include <sys/file.h>

#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>
#include <netlink/dump.h>

#include <string.h>
#include <printf.h>
#include <util.h>

#include "wienc.h"

char txbuf[512];
char rxbuf[8192-1024];

struct netlink nl;
int netlink;
static int nl80211;
static int scanseq;
static int freqreq;

int authstate;
int scanstate;

struct ap ap;

void setup_netlink(void)
{
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "mlme" },
		{ -1, "scan" },
		{  0, NULL } };
	int ret;

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));

	if((ret = nl_connect(&nl, NETLINK_GENERIC, 0)) < 0)
		fail("nl-connect", NULL, ret);

	if((nl80211 = query_family_grps(&nl, family, grps)) < 0)
		fail("NL family", family, nl80211);
	if(grps[0].id < 0)
		fail("NL group nl80211", grps[0].name, -ENOENT);

	if((ret = nl_subscribe(&nl, grps[0].id)) < 0)
		fail("NL subscribe nl80211", grps[0].name, -ret);
	if((ret = nl_subscribe(&nl, grps[1].id)) < 0)
		fail("NL subscribe nl80211", grps[1].name, -ret);

	netlink = nl.fd;
}

void quit(const char* msg, char* arg, int err)
{
	warn(msg, arg, err);

	if(authstate == AS_IDLE)
		goto out;
	if(authstate == AS_EXTERNAL)
		goto out;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_send_recv_ack(&nl);
out:
	unlink_control();
	_exit(0xFF);
}

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
	freqreq = 0;
}

int start_scan(int freq)
{
	struct nlattr* at;
	int ret;

	tracef("%s\n", __FUNCTION__);

	if(scanstate != SS_IDLE) {
		if(freq && !freqreq) {
			freqreq = freq;
			return 0;
		} else {
			return -EBUSY;
		}
	}

	nl_new_cmd(&nl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	if(freq) {
		at = nl_put_nest(&nl, NL80211_ATTR_SCAN_FREQUENCIES);
		nl_put_u32(&nl, 0, freq);
		nl_end_nest(&nl, at);
	}

	if((ret = nl_send(&nl)) < 0)
		return ret;

	scanstate = SS_SCANNING;
	scanseq = nl.seq;

	return 0;
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

static void trigger_scan_dump(void)
{
	int ret;

	tracef("%s\n", __FUNCTION__);

	nl_new_cmd(&nl, nl80211, NL80211_CMD_GET_SCAN, 0);
	nl_put_u64(&nl, NL80211_ATTR_IFINDEX, ifindex);
	
	if((ret = nl_send_dump(&nl)) < 0) {
		warn("nl-send", "scan dump", ret);
		scanstate = SS_IDLE;
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
	uint8_t* bssid;

	if(!(bss = nl_get_nest(msg, NL80211_ATTR_BSS)))
		return;
	if(!(bssid = nl_sub_of_len(bss, NL80211_BSS_BSSID, 6)))
		return;
	if(!(sc = grab_scan_slot(bssid)))
		return; /* out of scan slots */

	memcpy(sc->bssid, bssid, 6);
	sc->freq = get_i32_or_zero(bss, NL80211_BSS_FREQUENCY);
	sc->signal = get_i32_or_zero(bss, NL80211_BSS_SIGNAL_MBM);
	sc->type = 0;
	sc->flags &= ~SF_STALE;

	if((ies = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		parse_station_ies(sc, ies->payload, nl_attr_len(ies));
}

static void cmd_trigger_scan(struct nlgen* msg)
{
	tracef("%s\n", __FUNCTION__);

	if(scanstate != SS_SCANNING)
		return;

	mark_stale_scan_slots(msg);
}

static void cmd_scan_results(struct nlgen* msg)
{
	if(msg->nlm.flags & NLM_F_MULTI)
		parse_scan_result(msg);
	else if(scanstate == SS_SCANNING)
		trigger_scan_dump();
}

static void cmd_scan_aborted(struct nlgen* msg)
{
	warn(__FUNCTION__, NULL, 0);

	if(scanstate != SS_SCANNING)
		return;

	warn("scan aborted", NULL, 0);
	report_scan_fail();

	reset_scan_state();
	scanstate = SS_IDLE;
	scanseq = 0;
}

static void trigger_authentication(void)
{
	int authtype = 0;

	tracef("%s\n", __FUNCTION__);

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
	if(authstate != AS_IDLE)
		return -EBUSY;
	if(scanstate != SS_IDLE)
		return -EBUSY;

	reopen_rawsock();

	trigger_authentication();
	
	return 0;
}

static void trigger_associaction(void)
{
	tracef("%s\n", __FUNCTION__);

	nl_new_cmd(&nl, nl80211, NL80211_CMD_ASSOCIATE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);
	nl_put(&nl, NL80211_ATTR_MAC, ap.bssid, sizeof(ap.bssid));
	nl_put_u32(&nl, NL80211_ATTR_WIPHY_FREQ, ap.freq);
	nl_put(&nl, NL80211_ATTR_SSID, ap.ssid, ap.slen);

	nl_put(&nl, NL80211_ATTR_IE, ap.ies, ap.iesize);

	send_set_authstate(AS_ASSOCIATING);
}

static void snap_to_disabled(char* why)
{
	tracef("%s\n", why);
	//reset_eapol_state();
	//trigger_disconnect();
	//authstate = AS_IDLE;
	//opermode = OP_NEUTRAL;
}

/* See comments around prime_eapol_state() / allow_eapol_sends() on why
   this stuff works the way it does. Note that ASSOCIATE is the last
   command we issue over netlink, pretty everything else happens either
   on its own or through the rawsock. */

static void cmd_authenticate(struct nlgen* msg)
{
	tracef("%s\n", __FUNCTION__);

	if(authstate != AS_AUTHENTICATING)
		return snap_to_disabled("out-of-order AUTH");

	prime_eapol_state();

	trigger_associaction();
}

static void cmd_associate(struct nlgen* msg)
{
	tracef("%s\n", __FUNCTION__);

	if(authstate != AS_ASSOCIATING)
		return snap_to_disabled("out-of-order ASSOC");

	allow_eapol_sends();

	authstate = AS_CONNECTING;
}

static void cmd_connect(struct nlgen* msg)
{
	tracef("%s\n", __FUNCTION__);

	if(authstate != AS_CONNECTING)
		snap_to_disabled("out-of-order CONNECT");

	authstate = AS_CONNECTED;
}

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

static void trigger_disconnect(void)
{
	tracef("%s\n", __FUNCTION__);

	nl_new_cmd(&nl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	send_set_authstate(AS_DISCONNECTING);
}

void abort_connection(void)
{
	tracef("%s\n", __FUNCTION__);

	if(start_disconnect() >= 0)
		return;

	reassess_wifi_situation();
}

int start_disconnect(void)
{
	tracef("%s\n", __FUNCTION__);

	switch(authstate) {
		case AS_IDLE:
		case AS_NETDOWN:
			return -EALREADY;
		case AS_DISCONNECTING:
			return -EBUSY;
	}

	trigger_disconnect();

	return 0;
}

static void cmd_disconnect(struct nlgen* msg)
{
	tracef("%s\n", __FUNCTION__);

	reset_eapol_state();

	authstate = AS_IDLE;

	handle_disconnect();
}

static void drop_stale_scan_slots(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(sc->flags & SF_STALE)
			free_scan_slot(sc);
}

static void handle_scan_error(int err)
{
	tracef("%s %i\n", __FUNCTION__, err);

	reset_scan_state();
	report_scan_fail();
}

static void snap_to_netdown(void)
{
	reset_scan_state();

	if(rfkilled) {
		tracef("%s already rfkilled\n", __FUNCTION__);
		authstate = AS_IDLE;
	} else {
		tracef("%s no rfkill yet\n", __FUNCTION__);
		authstate = AS_NETDOWN;
		set_timer(1);
	}

	report_net_down();
}

static void handle_auth_error(int err)
{
	tracef("%s %i\n", __FUNCTION__, err);

	if(authstate == AS_DISCONNECTING) {
		authstate = AS_IDLE;
		reassess_wifi_situation();
	} else if(authstate == AS_AUTHENTICATING && err == -ENOENT) {
		authstate = AS_IDLE;
		start_scan(ap.freq);
	} else {
		abort_connection();
	}
}

static void genl_done(void)
{
	if(scanstate != SS_SCANDUMP)
		return;

	reset_scan_state();

	drop_stale_scan_slots();

	check_new_scan_results();

	report_scan_done();

	if(freqreq)
		reconnect_to_current_ap();
	else
		reassess_wifi_situation();
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
	{ NL80211_CMD_TRIGGER_SCAN,     cmd_trigger_scan },
	{ NL80211_CMD_NEW_SCAN_RESULTS, cmd_scan_results },
	{ NL80211_CMD_SCAN_ABORTED,     cmd_scan_aborted },
	{ NL80211_CMD_AUTHENTICATE,     cmd_authenticate },
	{ NL80211_CMD_ASSOCIATE,        cmd_associate    },
	{ NL80211_CMD_CONNECT,          cmd_connect      },
	{ NL80211_CMD_DISCONNECT,       cmd_disconnect   }
};

static void dispatch(struct nlgen* msg)
{
	const struct cmd* p;

	for(p = cmds; p < cmds + ARRAY_SIZE(cmds); p++)
		if(p->code == msg->cmd)
			return p->call(msg);

	//tracef("NL unhandled cmd %i\n", msg->cmd);
}

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
		fail("nl-recv", NULL, ret);

	while((msg = nl_get_nowait(&nl)))
		if(msg->type == NLMSG_DONE)
			genl_done();
		else if((err = nl_err(msg)))
			genl_error(err);
		else if(!(gen = nl_gen(msg)))
			;
		else if(!match_ifi(gen))
			;
		else dispatch(gen);

	nl_shift_rxbuf(&nl);
}
