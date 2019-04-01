#include <bits/errno.h>
#include <sys/file.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>

#include "common.h"
#include "vtctl.h"

static int open_proc_entry(int pid, char* key)
{
	FMTBUF(p, e, buf, 40);
	p = fmtstr(p, e, "/proc/");
	p = fmtint(p, e, pid);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, key);
	FMTEND(p, e);

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
	int* tty = uc_sub_int(vt, ATTR_TTY);
	int* pid = uc_sub_int(vt, ATTR_PID);

	if(!tty) return;

	FMTBUF(p, e, buf, 100);

	char* q = p;

	q = fmtstr(q, e, "tty");
	q = fmtint(p, e, *tty);
	q = fmtstr(q, e, (*tty == active ? "*" : ""));
	p = fmtpadr(p, e, 6, q);

	p = fmtstr(p, e, "  ");
	q = pid ? fmtint(p, e, *pid) : fmtstr(p, e, "-");
	p = fmtpadr(p, e, 5, q);

	p = fmtstr(p, e, "  ");

	p = maybe_put_comm(p, e, *pid);

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static int cmpvt(const void* a, const void* b)
{
	struct ucattr* at = *((struct ucattr**)a);
	struct ucattr* bt = *((struct ucattr**)b);

	int* pa = uc_sub_int(at, ATTR_TTY);
	int* pb = uc_sub_int(bt, ATTR_TTY);

	int ttya = pa ? *pa : 0;
	int ttyb = pb ? *pb : 0;

	if(ttya < ttyb)
		return -1;
	if(ttya > ttyb)
		return  1;

	return 0;
}

void dump_status(CTX, MSG)
{
	int active, *ap;
	struct ucattr* at;
	struct ucattr* vt;
	int nvts = 0, i = 0;

	init_output(ctx);

	if((ap = uc_get_int(msg, ATTR_TTY)))
		active = *ap;
	else
		active = -1;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(uc_is_nest(at, ATTR_VT))
			nvts++;

	struct ucattr* vts[nvts];

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if((vt = uc_is_nest(at, ATTR_VT)))
			vts[i++] = vt;

	qsort(vts, nvts, sizeof(void*), cmpvt);

	for(i = 0; i < nvts; i++)
		show_vt(ctx, vts[i], active);

	flush_output(ctx);
}
