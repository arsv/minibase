#include <sys/fprop.h>
#include <sys/proc.h>

#include <printf.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "dhcp.h"

int pids[4];
int npids;

static int spawn(int code, const char* script)
{
	struct dhcpopt* opt;
	int ret;

	if(!(opt = get_option(code, 0)))
		return 0;
	if(opt->len % 4)
		return 0;

	char* args[5];
	char** ap = args;
	char** ae = args + sizeof(args) - 1;
	
	char buf[200];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	*ap++ = p;
	p = fmtstr(p, e, ETCNET "/");
	p = fmtstr(p, e, script);
	p = fmtchar(p, e, '\0');

	if((ret = sys_access(*args, X_OK)) < 0)
		return ret;

	int len = opt->len;
	byte* ips = opt->payload;

	for(int i = 0; i <= len - 4; i += 4) {
		if(ap < ae && p < e)
			*ap++ = p;
		p = fmtip(p, e, ips + i);
		p = fmtchar(p, e, '\0');
	}

	*ap = NULL;

	if((ret = sys_fork()) < 0)
		return ret;

	if(ret == 0) {
		ret = sys_execve(*args, args, environ);
		fail(NULL, *args, ret);
	}

	return ret;
}

void run_scripts(void)
{
	int i, ret, status;
	int pids[3];

	pids[0] = spawn( 3, "dhcp-gw");
	pids[1] = spawn( 6, "dhcp-dns");
	pids[2] = spawn(42, "dhcp-ntp");

	while(1) {
		for(i = 0; i < 3; i++)
			if(pids[i] > 0)
				break;
		if(i >= 3)
			break;

		if((ret = sys_waitpid(-1, &status, 0)) < 0)
			fail("waitpid", NULL, ret);

		for(i = 0; i < 3; i++)
			if(pids[i] == ret)
				pids[i] = 0;
	}
}
