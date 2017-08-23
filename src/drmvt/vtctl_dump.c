#include <bits/errno.h>
#include <sys/file.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <fail.h>
#include <util.h>

#include "common.h"
#include "vtctl.h"

static int open_proc_entry(int pid, char* key)
{
	char buf[40];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, "/proc/");
	p = fmtint(p, e, pid);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, key);
	*p++ = '\0';

	return sys_open(buf, O_RDONLY);
}

static char* maybe_put_comm(char* p, char* e, int pid)
{
	int fd, rd;
	char buf[50];
	char* q;

	if((fd = open_proc_entry(pid, "comm")) < 0)
		return p;

	if((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		q = fmtraw(p, e, buf, rd);
		if(q > p && *(q-1) == '\n') q--;
		p = q;
	}

	sys_close(fd);

	return p;
}

static void show_vt(CTX, struct ucattr* vt, int active)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	char* q;

	int* tty = uc_sub_int(vt, ATTR_TTY);
	int* pid = uc_sub_int(vt, ATTR_PID);

	if(!tty) return;

	p = fmtstr(p, e, "tty");
	q = fmtint(p, e, *tty);
	q = fmtstr(q, e, (*tty == active ? "*" : ""));
	p = fmtpadr(p, e, 3, q);

	p = fmtstr(p, e, "  ");
	q = pid ? fmtint(p, e, *pid) : fmtstr(p, e, "-");
	p = fmtpadr(p, e, 5, q);

	p = fmtstr(p, e, "  ");

	p = maybe_put_comm(p, e, *pid);

	*p++ = '\n';

	output(ctx, buf, p - buf);
}

void dump_status(CTX, MSG)
{
	int active, *ap;
	struct ucattr* at;
	struct ucattr* vt;

	init_output(ctx);

	if((ap = uc_get_int(msg, ATTR_TTY)))
		active = *ap;
	else
		active = -1;
	
	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if((vt = uc_is_nest(at, ATTR_VT)))
			show_vt(ctx, vt, active);

	flush_output(ctx);
}
