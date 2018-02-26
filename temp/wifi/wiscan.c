#include <sys/socket.h>
#include <sys/creds.h>
#include <sys/sched.h>
#include <sys/file.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include <netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

/* Simple Wi-Fi scan tool, an easier to use equivalent of "iw ... scan".
   Mostly a netlink.h test atm, not really meant to be a permanent part
   of minitools. */

ERRTAG("wiscan");

/* All commands this tool sends are small (50 bytes or less), the output
   lines are formed and printed individually, but incoming packets may be
   surprisingly large. A single scan entry msg is 600..800 bytes, and there
   may be like 10 to 15 of them. Makes sense to get them all in a single
   recv call. */

char outbuf[512];
char txbuf[512];
char rxbuf[15*1024];

static int isprint(int c)
{
	return (c >= 0x02);
}

char* format_ssid(char* p, char* e, uint8_t len, const uint8_t *data)
{
	int i;

	p = fmtchar(p, e, '"');

	for (i = 0; i < len; i++)
		if(isprint(data[i])) {
			p = fmtchar(p, e, data[i]);
		} else {
			p = fmtstr(p, e, "\\x");
			p = fmtbyte(p, e, data[i]);
		}

	p = fmtchar(p, e, '"');

	return p;
}

/* IES is like a nested structure but it's not made of nlattr-s.
   Instead, they have a similar structure with one-byte len,
   one-byte type, and len does *not* include the header.
 
   Of all the data there, we only need key 0 which is BSS name. */

struct ies {
	uint8_t type;
	uint8_t len;
	uint8_t payload[];
};

char* format_ies(char* p, char* e, char* buf, int len)
{
	char* ptr = buf;
	char* end = buf + len;

	while(ptr < end) {
		struct ies* ie = (struct ies*) ptr;
		int ielen = sizeof(*ie) + ie->len;

		if(ptr + ielen > end)
			break;
		if(ie->type == 0)
			return format_ssid(p, e, ie->len, ie->payload);

		ptr += ielen;
	}

	return p;
}

char* format_mac(char* p, char* e, unsigned char mac[6])
{
	int i;

	for(i = 0; i < 6; i++) {
		if(i) p = fmtchar(p, e, ':');
		p = fmtbyte(p, e, mac[i]);
	}
	p = fmtchar(p, e, ' ');

	return p;
}

char* format_signal(char* p, char* e, int strength)
{
	p = fmtint(p, e, strength / 100);
	p = fmtchar(p, e, '.');
	p = fmtint(p, e, strength % 100);
	p = fmtchar(p, e, ' ');
	return p;
}

char* format_freq(char* p, char* e, int freq)
{
	p = fmtchar(p, e, '@');
	p = fmtchar(p, e, ' ');
	p = fmtint(p, e, freq);
	p = fmtchar(p, e, ' ');
	return p;
}

void print_scan_entry(struct nlgen* msg)
{
	struct nlattr* bss = nl_get_nest(msg, NL80211_ATTR_BSS);
	if(!bss) return;

	char* p = outbuf;
	char* e = outbuf + sizeof(outbuf) - 1;

	struct nlattr* at;
	unsigned char* id;
	int32_t* i32;

	if((id = nl_sub_of_len(bss, NL80211_BSS_BSSID, 6)))
		p = format_mac(p, e, id);
	else
		p = fmtstr(p, e, "XX:XX:XX:XX:XX:XX ");

	if((i32 = nl_sub_i32(bss, NL80211_BSS_SIGNAL_MBM)))
		p = format_signal(p, e, *i32);

	if((i32 = nl_sub_i32(bss, NL80211_BSS_FREQUENCY)))
		p = format_freq(p, e, *i32);

	if((at = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		p = format_ies(p, e, nl_payload(at), nl_paylen(at));

	*p++ = '\n';

	writeall(STDOUT, outbuf, p - outbuf);
}

/* NL80211_CMD_GET_SCAN requests current state of the stations list
   from the device. It may be used during scan, and long after scan,
   but only makes sense immediately after NL80211_CMD_NEW_SCAN_RESULTS.

   Unlike NL80211_CMD_TRIGGER_SCAN, this one works for non-privileged
   users, but the results are often useless. */

void request_ssid_list(struct netlink* nl, int nl80211, int ifindex)
{
	struct nlgen* gen;

	nl_new_cmd(nl, nl80211, NL80211_CMD_GET_SCAN, 0);
	nl_put_u64(nl, NL80211_ATTR_IFINDEX, ifindex);

	if(nl_send_dump(nl))
		fail("NL80211_CMD_GET_SCAN", NULL, nl->err);

	while(nl_recv_multi_into(nl, gen)) {
		if(gen->cmd == NL80211_CMD_NEW_SCAN_RESULTS)
			print_scan_entry(gen);
	} if(nl->err) {
		fail("NL80211_CMD_GET_SCAN", NULL, nl->err);
	}
}

/* CTRL_CMD_GETFAMILY provides both family id *and* multicast group ids
   we need for subscription. So we do it all in a single request. */

struct nlpair {
	const char* name;
	int id;
};

void query_nl_family(struct netlink* nl,
                     struct nlpair* fam, struct nlpair* mcast)
{
	struct nlgen* gen;
	struct nlattr* at;

	nl_new_cmd(nl, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1);
	nl_put_str(nl, CTRL_ATTR_FAMILY_NAME, fam->name);

	if(!(gen = nl_send_recv_genl(nl)))
		fail("CTRL_CMD_GETFAMILY", fam->name, nl->err);

	uint16_t* grpid = nl_get_u16(gen, CTRL_ATTR_FAMILY_ID);
	struct nlattr* groups = nl_get_nest(gen, CTRL_ATTR_MCAST_GROUPS);

	if(!grpid)
		fail("unknown nl family", fam->name, 0);
	if(!groups)
		fail("no mcast groups for", fam->name, 0);

	fam->id = *grpid;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* name = nl_sub_str(at, 1);
		uint32_t* id = nl_sub_u32(at, 2);

		if(!name || !id)
			continue;

		struct nlpair* mc;
		for(mc = mcast; mc->name; mc++)
			if(!strcmp(name, mc->name))
				mc->id = *id;
	}
}

void socket_subscribe(struct netlink* nl, int id, const char* name)
{
	int fd = nl->fd;
	int lvl = SOL_NETLINK;
	int opt = NETLINK_ADD_MEMBERSHIP;
	int ret;

	if((ret = sys_setsockopt(fd, lvl, opt, &id, sizeof(id))) < 0)
		fail("setsockopt NETLINK_ADD_MEMBERSHIP", name, ret);
}

int resolve_80211_subscribe_scan(struct netlink* nl)
{
	struct nlpair fam = { "nl80211", -1 };
	struct nlpair mcast[] = { { "scan", -1 }, { NULL, 0 } };

	query_nl_family(nl, &fam, mcast);

	struct nlpair* p;

	for(p = mcast; p->name; p++)
		if(p->id >= 0)
			socket_subscribe(nl, p->id, p->name);
		else
			fail("unknown 802.11 mcast group", p->name, 0);

	return fam.id;
}

/* The following strongly assumes that scan results arrive after ACK
   or ERR for NL80211_CMD_TRIGGER_SCAN. It's not clear whether there
   are any guarantees on this.
 
   Note NL80211_CMD_NEW_SCAN_RESULTS and NL80211_CMD_SCAN_ABORTED here
   and *not* replies to NL80211_CMD_TRIGGER_SCAN!
   Instead those packets are mcast notifications (seq=0) we've subscribed
   to earlier in resolve_80211_subscribe_scan(). */

void request_scan(struct netlink* nl, int nl80211, int ifindex)
{
	struct nlgen* msg;
	int* idx;

	nl_new_cmd(nl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u64(nl, NL80211_ATTR_IFINDEX, ifindex);

	if(nl_send_recv_ack(nl))
		fail("NL80211_CMD_TRIGGER_SCAN", NULL, nl->err);

	while((msg = nl_recv_genl(nl))) {
		if(!(idx = nl_get_i32(msg, NL80211_ATTR_IFINDEX)))
			continue;
		if(*idx != ifindex)
			continue;
		if(msg->cmd == NL80211_CMD_SCAN_ABORTED)
			fail("aborted", NULL, 0);
		if(msg->cmd == NL80211_CMD_NEW_SCAN_RESULTS)
			break;
	} if(nl->err) {
		fail("nl-recv", NULL, nl->err);
	}
}

static int query_interface(struct netlink* nl, int nl80211, char* name)
{
	struct nlgen* msg;
	int* idx;

	int ifindex = -1;

	nl_new_cmd(nl, nl80211, NL80211_CMD_GET_INTERFACE, 0);
	if(nl_send_dump(nl))
		fail("nl-send", NULL, nl->err);

	while((msg = nl_recv_genl_multi(nl))) {
		if(!(idx = nl_get_i32(msg, NL80211_ATTR_IFINDEX)))
			continue;
		if(name) {
			char* nn = nl_get_str(msg, NL80211_ATTR_IFNAME);
			if(!nn || strcmp(nn, name)) continue;
			ifindex = *idx;
			break;
		} else if(ifindex < 0 || ifindex > *idx) {
			ifindex = *idx;
		}
	} if(nl->err) {
		fail("nl-recv", NULL, nl->err);
	}

	if(ifindex < 0) {
		if(name)
			fail("interface not found:", name, 0);
		else
			fail("no interfaces to scan on", NULL, 0);
	}

	return ifindex;
}

int main(int argc, char** argv)
{
	struct netlink nl;
	char* ifname = NULL;
	int ret;

	if(argc > 2)
		fail("too many arguments", NULL, 0);
	if(argc == 2)
		ifname = argv[1];

	nl_init(&nl);
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));

	if((ret = nl_connect(&nl, NETLINK_GENERIC, 0)) < 0)
		fail("netlink connect", NULL, ret);

	int nl80211 = resolve_80211_subscribe_scan(&nl);
	int ifindex = query_interface(&nl, nl80211, ifname);

	request_scan(&nl, nl80211, ifindex);

	request_ssid_list(&nl, nl80211, ifindex);

	return 0;
}
