#include <sys/socket.h>
#include <sys/creds.h>
#include <sys/sched.h>
#include <sys/file.h>

#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>
#include <main.h>

#include <netlink.h>
#include <netlink/attr.h>
#include <netlink/pack.h>
#include <netlink/recv.h>
#include <netlink/dump.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

/* Simple Wi-Fi scan tool, an easier to use equivalent of "iw ... scan".
   Mostly a netlink.h test atm, not really meant to be a permanent part
   of minitools. */

ERRTAG("wiscan");

#define ST_FAMILY   1
#define ST_SCANNING 2
#define ST_DUMPING  3

/* All commands this tool sends are small (50 bytes or less), the output
   lines are formed and printed individually, but incoming packets may be
   surprisingly large. A single scan entry msg is 600..800 bytes, and there
   may be like 10 to 15 of them. Makes sense to get them all in a single
   recv call. */

char outbuf[512];
char txbuf[512];
char rxbuf[15*1024];

struct ncbuf nc;
struct nrbuf nr;

int netlink;
int nl80211;
int ifindex;

int state;

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

static char* format_ies(char* p, char* e, char* buf, int len)
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

static char* format_mac(char* p, char* e, unsigned char mac[6])
{
	int i;

	for(i = 0; i < 6; i++) {
		if(i) p = fmtchar(p, e, ':');
		p = fmtbyte(p, e, mac[i]);
	}
	p = fmtchar(p, e, ' ');

	return p;
}

static char* format_signal(char* p, char* e, int strength)
{
	p = fmtint(p, e, strength / 100);
	p = fmtchar(p, e, '.');
	p = fmtint(p, e, strength % 100);
	p = fmtchar(p, e, ' ');
	return p;
}

static char* format_freq(char* p, char* e, int freq)
{
	p = fmtchar(p, e, '@');
	p = fmtchar(p, e, ' ');
	p = fmtint(p, e, freq);
	p = fmtchar(p, e, ' ');
	return p;
}

static void handle_scan_entry(struct nlgen* msg)
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

static void send_request(void)
{
	int ret, fd = netlink;

	if((ret = nc_send(fd, &nc)) < 0)
		fail("send", "NETLINK", ret);
}

/* NL80211_CMD_GET_SCAN requests current state of the stations list
   from the device. It may be used during scan, and long after scan,
   but only makes sense immediately after NL80211_CMD_NEW_SCAN_RESULTS.

   Unlike NL80211_CMD_TRIGGER_SCAN, this one works for non-privileged
   users, but the results are often useless. */

static void handle_scan_results(void)
{
	nc_header(&nc, nl80211, NLM_F_DUMP, 0);
	nc_gencmd(&nc, NL80211_CMD_GET_SCAN, 0);
	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifindex);

	send_request();

	state = ST_DUMPING;
}

static void request_scan(void)
{
	nc_header(&nc, nl80211, 0, 0);
	nc_gencmd(&nc, NL80211_CMD_TRIGGER_SCAN, 0);
	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifindex);

	send_request();

	state = ST_SCANNING;
}

void socket_subscribe(int id, const char* name)
{
	int fd = netlink;
	int lvl = SOL_NETLINK;
	int opt = NETLINK_ADD_MEMBERSHIP;
	int ret;

	if((ret = sys_setsockopt(fd, lvl, opt, &id, sizeof(id))) < 0)
		fail("setsockopt NETLINK_ADD_MEMBERSHIP", name, ret);
}

static int find_group(struct nlattr* groups, char* name)
{
	struct nlattr* at;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* gn = nl_sub_str(at, 1);
		uint32_t* id = nl_sub_u32(at, 2);

		if(!gn || !id)
			continue;
		if(strcmp(gn, name))
			continue;

		return *id;
	}

	return -ENOENT;
}

static void handle_family(struct nlgen* msg)
{
	int id, ret;

	uint16_t* grpid = nl_get_u16(msg, CTRL_ATTR_FAMILY_ID);
	struct nlattr* groups = nl_get_nest(msg, CTRL_ATTR_MCAST_GROUPS);

	if(!grpid)
		fail("nl80211 missing", NULL, 0);
	if(!groups)
		fail("no event groups", NULL, 0);

	if((id = find_group(groups, "scan")) < 0)
		fail("no scan group", NULL, 0);

	if((ret = nl_subscribe(netlink, id)) < 0)
		fail("cannot subscribe scan", NULL, ret);

	nl80211 = *grpid;

	request_scan();
}

static void request_family(void)
{
	char* name = "nl80211";

	nc_header(&nc, GENL_ID_CTRL, 0, 0);
	nc_gencmd(&nc, CTRL_CMD_GETFAMILY, 1);
	nc_put_str(&nc, CTRL_ATTR_FAMILY_NAME, name);

	send_request();

	state = ST_FAMILY;
}

static void open_socket(void)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW;
	int protocol = NETLINK_GENERIC;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ret;

	if((fd = sys_socket(domain, type, protocol)) < 0)
		fail("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		fail("bind", "NETLINK", ret);

	netlink = fd;
}

static void handle_genl(struct nlgen* msg)
{
	int cmd = msg->cmd;

	if(state == ST_FAMILY) {
		if(cmd == CTRL_CMD_NEWFAMILY)
			return handle_family(msg);
	} else if(state == ST_SCANNING) {
		if(cmd == NL80211_CMD_TRIGGER_SCAN)
			return; /* no need to handle this */
		if(cmd == NL80211_CMD_NEW_SCAN_RESULTS)
			return handle_scan_results();
		if(cmd == NL80211_CMD_SCAN_ABORTED)
			fail("scan aborted", NULL, 0);
	} else if(state == ST_DUMPING) {
		if(cmd == NL80211_CMD_NEW_SCAN_RESULTS)
			return handle_scan_entry(msg);
	}

	fail("unexpected reply", NULL, cmd);
}

static void handle_events(void)
{
	int ret, fd = netlink;
	struct nlmsg* msg;
	struct nlerr* err;
	struct nlgen* gen;
recv:
	if((ret = nr_recv(fd, &nr)) < 0)
		fail("recv", "NETLINK", ret);
next:
	if(!(msg = nr_next(&nr)))
		goto recv;
	if((err = nl_err(msg)))
		fail(NULL, NULL, err->errno);
	if(msg->type == NLMSG_DONE && state == ST_DUMPING)
		return;
	if(!(gen = nl_gen(msg)))
		fail(NULL, NULL, -EBADMSG);

	handle_genl(gen);

	goto next;
}

static void resolve_iface(char* name)
{
	int ret, fd = netlink;

	if((ret = getifindex(fd, name)) < 0)
		fail(NULL, name, ret);

	ifindex = ret;
}

int main(int argc, char** argv)
{
	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 2)
		fail("too many arguments", NULL, 0);

	nc_buf_set(&nc, txbuf, sizeof(txbuf));
	nr_buf_set(&nr, rxbuf, sizeof(rxbuf));

	open_socket();
	resolve_iface(argv[1]);
	request_family();

	handle_events();

	return 0;
}
