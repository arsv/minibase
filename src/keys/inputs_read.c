#include <bits/input.h>
#include <sys/file.h>

#include <format.h>
#include <util.h>

#include "inputs.h"

static void dump(char* s, char* p)
{
	*p++ = '\n';
	writeall(STDOUT, s, p - s);
}

static char* putcode(char* p, char* e, const struct ev* ev, int code)
{
	char* name;

	p = fmtstr(p, e, ev->tag);
	p = fmtstr(p, e, " ");
	p = fmtint(p, e, code);

	if((name = getname(ev, code))) {
		p = fmtstr(p, e, "/");
		p = fmtstr(p, e, name);
	}

	p = fmtstr(p, e, " ");

	return p;
}

static void dump_keylike(struct event* ee, const struct ev* ev)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = putcode(p, e, ev, ee->code);

	switch(ee->value) {
		case 0: p = fmtstr(p, e, "released"); break;
		case 1: p = fmtstr(p, e, "pressed"); break;
		case 2: p = fmtstr(p, e, "repeat"); break;
		default:
			p = fmtstr(p, e, "event ");
			p = fmtint(p, e, ee->value);
	}

	dump(buf, p);
}

static void dump_ptrlike(struct event* ee, const struct ev* ev)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = putcode(p, e, ev, ee->code);
	p = fmtint(p, e, ee->value);

	dump(buf, p);
}

static void dump_event(struct event* ee)
{
	switch(ee->type) {
		case EV_SYN: return;
		case EV_MSC: return;
		case EV_KEY: dump_keylike(ee, &ev_key); return;
		case EV_SW:  dump_keylike(ee, &ev_sw); return;
		case EV_REL: dump_ptrlike(ee, &ev_rel); return;
		case EV_ABS: dump_ptrlike(ee, &ev_abs); return;
	}
}

void read_events(char* _, int fd)
{
	char buf[256];
	char* ptr;
	int size = sizeof(struct event);
	int rd;

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0)
		for(ptr = buf; ptr < buf + rd; ptr += size)
			dump_event((struct event*) ptr);
}
