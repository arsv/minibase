#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/fprop.h>
#include <sys/timer.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "dhconf.h"

void kill_wait_pid(CTX)
{
	int pid = ctx->pid;
	int ret, status;

	sys_alarm(2);

	if(pid > 0) {
		if((ret = sys_kill(pid, SIGTERM)) < 0)
			;
		else sys_waitpid(pid, &status, 0);
	}
}

static int spawn(CTX, char* args[])
{
	int pid, ret;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, BASE_ETC "/net/");
	p = fmtstr(p, e, args[0]);
	FMTEND(p, e);

	args[0] = path;

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	if((pid = sys_fork()) < 0)
		return pid;

	if(pid == 0) {
		char** envp = ctx->environ;

		if((ret = sys_execve(*args, args, envp)) < 0)
			warn("exec", *args, ret);

		_exit(0xFF);
	}

	ctx->pid = pid;

	return pid;
}

static int spawn_ips(CTX, char* script, int key)
{
	struct dhcpopt* opt;
	uint i, n, k = 2;
	char* args[10];

	if(!(opt = get_ctx_option(ctx, key)))
		return 0;
	if((n = opt->len) % 4)
		return 0;

	byte* ips = opt->payload;

	args[0] = script;
	args[1] = ctx->ifname;

	FMTBUF(p, e, buf, 100);

	for(i = 0; i < n; i += 4) {
		char* q = p;
		p = fmtip(p, e, &ips[i]);
		p = fmtchar(p, e, '\0');

		if(p >= e) break;
		if(k >= ARRAY_SIZE(args) - 1) break;

		args[k++] = q;
	}

	args[k] = NULL;

	return spawn(ctx, args);
}

static int run_gw_script(CTX)
{
	char* args[4];
	byte* ip;

	if(!(ip = get_ctx_opt(ctx, DHCP_ROUTER_IP, 4)))
		return 0;

	FMTBUF(p, e, buf, 30);
	p = fmtip(p, e, ip);
	FMTEND(p, e);

	args[0] = "dhcp-gw";
	args[1] = ctx->ifname;
	args[2] = buf;
	args[3] = NULL;

	return spawn(ctx, args);
}

static int run_dns_script(CTX)
{
	return spawn_ips(ctx, "dhcp-dns", DHCP_NAME_SERVERS);
}

static int run_ntp_script(CTX)
{
	return spawn_ips(ctx, "dhcp-dns", DHCP_TIME_SERVERS);
}

typedef int (*fh)(CTX);

static const fh scripts[] = {
	run_gw_script,
	run_dns_script,
	run_ntp_script
};

static void run_next_script(CTX)
{
	uint i = ctx->script;

	while(i < ARRAY_SIZE(scripts))
		if(scripts[i++](ctx) > 0)
			break;

	ctx->script = i;
}

void check_child(CTX)
{
	int pid, status;

	if((pid = sys_waitpid(-1, &status, WNOHANG)) <= 0)
		return;
	if(pid != ctx->pid)
		return;

	ctx->pid = -1;

	run_next_script(ctx);
}

void proceed_with_scripts(CTX)
{
	ctx->script = 0;

	if(ctx->pid > 0)
		return;

	run_next_script(ctx);
}
