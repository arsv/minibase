#include <sys/socket.h>
#include <sys/file.h>

#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <util.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

/* Command sequence here:

	<- NL80211_CMD_TRIGGER_SCAN      start_scan
	-> NL80211_CMD_TRIGGER_SCAN      nlm_trigger_scan
	-> NL80211_CMD_NEW_SCAN_RESULTS  nlm_scan_results
	<- NL80211_CMD_GET_SCAN          trigger_scan_dump
	-> NL80211_CMD_NEW_SCAN_RESULTS* nlm_scan_results
	-> NL80211_CMD_NEW_SCAN_RESULTS* nlm_scan_results
	...
	-> NL80211_CMD_NEW_SCAN_RESULTS* nlm_scan_results
	-> NLMSG_DONE                    genl_done

   The card reports start-of-scan with frequencies to be scanned,
   performs the scan, then reports end-of-scan. A separate command
   is needed to fetch the results.

   Scan results are stored internally on the card, but this storage
   is typically *very* short-term (seconds, single-digit) and therefore
   cannot be relied upon. */

/* scanstate */
#define SS_IDLE            0
#define SS_SCANNING        1
#define SS_SCANDUMP        2

uint scanseq;
int scanstate;
static int scanreq;

void reset_scan_state(void)
{
	scanstate = SS_IDLE;
	scanreq = 0;
}

static int trigger_scan(int freq)
{
	struct nlattr* at;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifindex);

	if(freq) {
		at = nl_put_nest(&nl, NL80211_ATTR_SCAN_FREQUENCIES);
		nl_put_u32(&nl, 0, freq); /* FREQUENCIES[0] = freq */
		nl_end_nest(&nl, at);
	}

	scanseq = nl.seq;

	return nl_send(&nl);
}

/* The weird logic below handles the cases when a re-scan or a routine
   scheduled scan coincides with a user-requested full range scan. */

int start_scan(int freq)
{
	int ret;

	if(netlink <= 0)
		return -ENODEV;

	if(scanstate == SS_IDLE) /* no ongoing scan, great */
		;
	else if(scanreq) /* ongoing single-freq scan, bad */
		return -EBUSY;
	else /* ongoing whole-range scan */
		return 0;

	if((ret = trigger_scan(freq)) < 0)
		return ret;

	scanreq = freq;
	scanstate = SS_SCANNING;

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

	if(!scanreq)
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

	byte* data = nl_payload(ies);
	int ieslen = nl_paylen(ies);

	if(sc->ies && sc->ieslen <= ieslen)
		memcpy(sc->ies, data, ieslen);
	else if((stored = heap_store(data, ieslen)))
		sc->ies = stored;
	else return; /* cannot overwrite and cannot store */

	sc->ieslen = ieslen;
}

/* NL80211_CMD_TRIGGER_SCAN arrives with a list of frequencies being
   scanned. We use it to mark and later remove stale scan entries.
   The list may not cover the whole range, e.g. if it's a single-freq
   scans, so the point is to mark only the entries being re-scanned. */

void nlm_trigger_scan(MSG)
{
	if(scanstate != SS_SCANNING)
		return;

	mark_stale_scan_slots(msg);

	//report_scanning();
}

/* Non-MULTI scan results command means the card is done scanning,
   and it comes empty. We then send a dump request, which results
   in a bunch of messages with the same NL80211_CMD_NEW_SCAN_RESULTS code
   but also with the MULTI flag set. The dump ends with a NLMSG_DONE. */

void nlm_scan_results(MSG)
{
	if(msg->nlm.flags & NLM_F_MULTI)
		parse_scan_result(msg);
	else if(scanstate == SS_SCANNING)
		trigger_scan_dump();
}

void nlm_scan_aborted(MSG)
{
	if(scanstate != SS_SCANNING)
		return;

	reset_scan_state();

	scan_ended(-EINTR);
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

void nlm_scan_done(void)
{
	if(scanstate != SS_SCANDUMP)
		return;

	reset_scan_state();

	drop_stale_scan_slots();
	maybe_trim_heap();

	scan_ended(0);
}

/* Netlink errors caused by scan-related commands; we track those
   with scanseq. There's really not that much to do here, if a scan
   fails (and it's not ENETDOWN which gets handled separately). */

void nlm_scan_error(int err)
{
	if(!err) return; /* stray ACK */

	reset_scan_state();

	scan_ended(err);
}
