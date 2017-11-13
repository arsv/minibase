#include <bits/errno.h>
#include <sys/proc.h>

#include <netlink.h>
#include <netlink/genl.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

ERRTAG("acpid");

static const char confdir[] = "/etc/acpi";

struct top {
	struct netlink nl;
	char txbuf[50];
	char rxbuf[2048];
};

struct acpievent {
        char cls[20];
        char bus[15];
        int type;
        int data;
};

static const struct action {
	char cls[20];
	int data;
	char script[20];
} actions[] = {
	{ "ac_adapter",   0, "battery" },
	{ "ac_adapter",   1, "acpower" },
	{ "button/sleep", 1, "sleep"   },
	{ "button/power", 1, "power"   }
};

static void spawn_handler(const char* script, char** envp)
{
	char path[sizeof(confdir)+strlen(script)+5];
	char* p = path;
	char* e = path + sizeof(path) - 1;
	int pid, status, ret;

	p = fmtstr(p, e, confdir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, script);
	*p++ = '\0';

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	} else if(pid == 0) {
		char* argv[] = { path, NULL };
		ret = sys_execve(*argv, argv, envp);
		if(ret != -ENOENT && ret != -ENOTDIR)
			fail("exec", path, ret);
		_exit(ret ? -1 : 0);
	}

	/* TODO: timeout here? */
	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		warn("waitpid", path, ret);
}

static const struct action* event_action(struct acpievent* evt)
{
	const struct action* act = actions;
	const struct action* end = ARRAY_END(actions);
	int cmplen = sizeof(evt->cls);

	for(; act < end; act++) {
		if(strncmp(act->cls, evt->cls, cmplen))
			continue;
		if(act->data != evt->data)
			continue;
		
		return act;
	}

	return NULL;
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

static void setup_netlink(struct top* ctx)
{
	char* family = "acpi_event";
	char* group = "acpi_mc_group";
	struct nlpair grps[] = { { -1, group }, { 0, NULL } };
	struct netlink* nl = &ctx->nl;
	int ret, gid, fid;

	nl_init(nl);
	nl_set_txbuf(nl, ctx->txbuf, sizeof(ctx->txbuf));
	nl_set_rxbuf(nl, ctx->rxbuf, sizeof(ctx->rxbuf));

	if((ret = nl_connect(nl, NETLINK_GENERIC, 0)) < 0)
		fail("NL connect", "genl", ret);

	if((fid = query_family_grps(nl, family, grps)) < 0)
		fail("NL family", family, fid);

	if((gid = grps[0].id) < 0)
		fail("NL group", group, -ENOENT);

	if((ret = nl_subscribe(nl, gid)) < 0)
		fail("NL subscribe ", group, ret);
}

int main(int argc, char** argv, char** envp)
{
	(void)argv;

	struct top context, *ctx = &context;
	memzero(ctx, sizeof(*ctx));

	struct netlink* nl = &ctx->nl;
	struct nlmsg* nlm;
	struct acpievent* evt;
	const struct action* act;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	setup_netlink(ctx);

	while((nlm = nl_recv(nl))) {
		if(!(evt = get_acpi_event(nlm)))
			continue;
		if(!(act = event_action(evt)))
			continue;

		spawn_handler(act->script, envp);
	}

	return 0;
}
