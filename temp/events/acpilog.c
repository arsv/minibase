#include <bits/errno.h>
#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/dump.h>

#include <format.h>
#include <string.h>
#include <main.h>
#include <util.h>

#define CTX struct top* ctx

ERRTAG("acpilog");

struct top {
	struct netlink nl;
	char txbuf[50];
	char rxbuf[1024];
};

struct acpievent {
        char cls[20];
        char bus[15];
        int type;
        int data;
};

static void setup_netlink(CTX)
{
	char* family = "acpi_event";
	char* group = "acpi_mc_group";
	struct nlpair grps[] = { { -1, group }, { 0, NULL } };
	struct netlink* nl = &ctx->nl;
	int gid, fid, ret;

	nl_init(nl);
	nl_set_txbuf(nl, ctx->txbuf, sizeof(ctx->txbuf));
	nl_set_rxbuf(nl, ctx->rxbuf, sizeof(ctx->rxbuf));

	if((ret = nl_connect(nl, NETLINK_GENERIC, 0)) < 0)
		fail("nl-connect", "genl", ret);
	if((fid = query_family_grps(nl, family, grps)) < 0)
		fail("NL family", family, fid);
	if((gid = grps[0].id) < 0)
		fail("NL group", group, -ENOENT);

	if((ret = nl_subscribe(nl, gid)) < 0)
		fail("NL subscribe nl80211", group, ret);
}

static void dump_event(struct acpievent* evt)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	int clslen = strnlen(evt->cls, sizeof(evt->cls));
	int buslen = strnlen(evt->bus, sizeof(evt->bus));

	p = fmtraw(p, e, evt->cls, clslen);
	p = fmtstr(p, e, " ");
	p = fmtraw(p, e, evt->bus, buslen);
	p = fmtstr(p, e, " ");

	p = fmtstr(p, e, "type=");
	p = fmtint(p, e, evt->type);
	p = fmtstr(p, e, " ");

	p = fmtstr(p, e, "data=");
	p = fmtint(p, e, evt->data);
	*p++ = '\n';

	writeall(STDOUT, buf, p - buf);
}

static struct acpievent* get_acpi_event(struct nlmsg* nlm)
{
	struct nlgen* msg;
	struct acpievent* evt;

	if(!(msg = nl_gen(nlm)))
		return NULL;
	if(msg->cmd != 1)
		return NULL;

	return nl_get_of_len(msg, 1, sizeof(*evt));
}

int main(noargs)
{
	struct top context, *ctx = &context;
	memzero(ctx, sizeof(*ctx));

	struct netlink* nl = &ctx->nl;
	struct nlmsg* nlm;
	struct acpievent* evt;

	setup_netlink(ctx);

	while((nlm = nl_recv(nl)))
		if((evt = get_acpi_event(nlm)))
			dump_event(evt);

	return 0;
}
