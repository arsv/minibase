#include <bits/input.h>
#include <bits/errno.h>
#include <bits/major.h>

#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/dents.h>

#include <errtag.h>
#include <output.h>
#include <printf.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "inputs.h"

ERRTAG("inputs");
ERRLIST(NEACCES NEPERM NENOTTY NENODEV NENOTDIR NEINVAL);

#define OPTS "ar"
#define OPT_a (1<<0)
#define OPT_r (1<<1)

static int prefixed(char* name, char* pref)
{
	int plen = strlen(pref);

	if(strncmp(name, pref, plen))
		return 0;

	return plen;
}

/* uevent parsing */

static int read_uevent(int eid, char* buf, int size)
{
	int fd, rd;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/class/input/event");
	p = fmtint(p, e, eid);
	p = fmtstr(p, e, "/device/uevent");
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0) {
		warn(NULL, path, fd);
		return fd;
	}

	if((rd = sys_read(fd, buf, size)) < 0) {
		warn("read", path, rd);
		return rd;
	}

	sys_close(fd);

	if(rd >= size) {
		rd = -E2BIG;
		warn(NULL, path, rd);
	}

	return rd;
}

static void set_name(struct info* in, char* str)
{
	if(*str == '"') {
		int len = strlen(str);

		if(len > 0 && str[len-1] == '"')
			str[len-1] = '\0';
		
		str[0] = '"';
		str++;
	}

	FMTUSE(p, e, in->name, sizeof(in->name));
	p = fmtstr(p, e, str);
	FMTEND(p, e);
}

static void set_bits(ulong* mask, int n, char* str)
{
	char* p = str;
	ulong v;

	while(*p && (p = parsexlong(p, &v))) {
		for(int i = n - 1; i >= 1; i--)
			mask[i] = mask[i-1];
		mask[0] = v;

		while(*p == ' ') p++;
	}
}

#define IN(k) in->k, ARRAY_SIZE(in->k)

static int load_uevent(int id, struct info* in)
{
	char buf[2048];
	int rd;

	memzero(in, sizeof(*in));

	if((rd = read_uevent(id, buf, sizeof(buf)-1)) < 0)
		return rd;

	char* p = buf;
	char* q;
	char* e = buf + rd;
	int pl;

	while(p < e) {
		q = strecbrk(p, e, '\n'); *q = '\0';

		if((pl = prefixed(p, "NAME=")) > 0)
			set_name(in, p + pl);
		else if((pl = prefixed(p, "KEY=")) > 0)
			set_bits(IN(key), p + pl);
		else if((pl = prefixed(p, "ABS=")) > 0)
			set_bits(IN(abs), p + pl);
		else if((pl = prefixed(p, "REL=")) > 0)
			set_bits(IN(rel), p + pl);
		else if((pl = prefixed(p, "LED=")) > 0)
			set_bits(IN(led), p + pl);
		else if((pl = prefixed(p, "SW=")) > 0)
			set_bits(IN(sw), p + pl);

		p = q + 1;
	}

	return 0;
}

/* Device list and info output */

static char* get_code_name(const struct ev* ev, int k)
{
	if(k < 0 || k >= ev->count)
		return NULL;
	return (char*)ev->names[k];
}

static void put_raw(struct bufout* bo, int* col, char* str, int len)
{
	bufout(bo, str, len);
	*col += len;
}

static void put_str(struct bufout* bo, int* col, char* str)
{
	put_raw(bo, col, str, strlen(str));
}

static void put_nl(struct bufout* bo, int* col)
{
	bufout(bo, "\n", 1);
	*col = 0;
}

static void put_bit(struct bufout* bo, int* col, const struct ev* ev, int code)
{
	char* desc = get_code_name(ev, code);

	FMTBUF(p, e, buf, 40);

	p = fmtint(p, e, code);

	if(desc) {
		p = fmtstr(p, e, "=");
		p = fmtstr(p, e, desc);
	}

	FMTEND(p, e);

	long len = p - buf;

	if(*col + len + 1 < 70) {
		put_str(bo, col, " ");
	} else {
		put_nl(bo, col);
		put_str(bo, col, "      ");
	}

	put_raw(bo, col, buf, len);
}

static void put_tag(struct bufout* bo, int* col, const struct ev* ev)
{
	put_str(bo, col, "  ");
	put_str(bo, col, (char*)ev->tag);
	put_str(bo, col, "");
}

static void dump_event_type(struct bufout* bo, ulong* mask, int n, const struct ev* ev)
{
	int i, b;
	int col = 0;

	ulong word;
	int bits = 8*sizeof(*mask);

	if(!nonzero(mask, n*sizeof(*mask)))
		return;

	put_tag(bo, &col, ev);

	for(i = 0; i < n; i++) {
		if(!(word = mask[i]))
			continue;
		for(b = 0; b < bits; b++) {
			if(!(word & (1UL << b)))
				continue;
			put_bit(bo, &col, ev, i*bits + b);
		}
	}

	bufout(bo, "\n", 1);
}

static void describe(struct bufout* bo, int id, int opts)
{
	struct info info, *in = &info;

	if(load_uevent(id, in) < 0)
		return;

	FMTBUF(p, e, buf, 120);

	p = fmtstr(p, e, "/dev/input/event");
	p = fmtint(p, e, id);
	p = fmtstr(p, e, ": ");

	if(info.name[0])
		p = fmtstr(p, e, info.name);
	else
		p = fmtstr(p, e, "(unnamed)");

	FMTENL(p, e);

	bufout(bo, buf, p - buf);

	if(!(opts & OPT_a)) return;

	dump_event_type(bo, IN(key), &ev_key);
	dump_event_type(bo, IN(rel), &ev_rel);
	dump_event_type(bo, IN(abs), &ev_abs);
	dump_event_type(bo, IN(led), &ev_led);
	dump_event_type(bo, IN(sw), &ev_sw);
}

static int name2id(char* name)
{
	int pl, devn;
	char* q = name;

	if((pl = prefixed(name, "/dev/input/")))
		q += pl;
	if((pl = prefixed(name, "event")))
		q += pl;

	if(!(q = parseint(q, &devn)) || *q)
		return -1;

	return devn;
}

static int namecheck(char* name)
{
	int id;

	if(prefixed(name, "input") > 0)
		fail("need eventN not inputN device", NULL, 0);
	if((id = name2id(name)) < 0)
		fail("unexpected device name", name, 0);

	return id;
}

static void list_args(int argc, char** args, int opts)
{
	int i, ids[argc];
	char buf[2048];
	struct bufout bo = {
		.fd = STDOUT,
		.buf = buf,
		.ptr = 0,
		.len = sizeof(buf)
	};

	for(i = 0; i < argc; i++)
		ids[i] = namecheck(args[i]);

	for(i = 0; i < argc; i++)
		describe(&bo, ids[i], opts);

	bufoutflush(&bo);
}

static void list_masked(byte* mask, int maxid, int opts)
{
	int id;
	char buf[2048];
	struct bufout bo = {
		.fd = STDOUT,
		.buf = buf,
		.ptr = 0,
		.len = sizeof(buf)
	};

	for(id = 0; id <= maxid; id++)
		if(mask[id/8] & (1 << (id%8)))
			describe(&bo, id, opts);

	bufoutflush(&bo);
}

static void list_all_inputs(int opts)
{
	int fd, rd, id;
	char* dir = "/sys/class/input";
	char buf[2048];

	byte mask[64]; /* at most 512 inputs devices */
	int maxid = 0;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	memzero(mask, sizeof(mask));

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			if(!de->reclen) break;

			ptr += de->reclen;

			if((id = name2id(de->name)) < 0)
				continue;
			if(id > 8*sizeof(mask))
				continue;

			mask[id/8] |= (1 << (id%8));

			if(id > maxid) maxid = id;
		}
	}
	
	if(rd < 0) fail("read", dir, rd);

	list_masked(mask, maxid, opts);
}

/* Event monitor */

static char* putcode(char* p, char* e, const struct ev* ev, int code)
{
	char* name;

	p = fmtstr(p, e, ev->tag);
	p = fmtstr(p, e, " ");
	p = fmtint(p, e, code);

	if((name = get_code_name(ev, code))) {
		p = fmtstr(p, e, "/");
		p = fmtstr(p, e, name);
	}

	p = fmtstr(p, e, " ");

	return p;
}

static void dump_keylike(struct event* ee, const struct ev* ev)
{
	FMTBUF(p, e, buf, 100);

	p = putcode(p, e, ev, ee->code);

	switch(ee->value) {
		case 0: p = fmtstr(p, e, "released"); break;
		case 1: p = fmtstr(p, e, "pressed"); break;
		case 2: p = fmtstr(p, e, "repeat"); break;
		default:
			p = fmtstr(p, e, "event ");
			p = fmtint(p, e, ee->value);
	}

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

static void dump_ptrlike(struct event* ee, const struct ev* ev)
{
	FMTBUF(p, e, buf, 100);

	p = putcode(p, e, ev, ee->code);
	p = fmtint(p, e, ee->value);

	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
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

static int open_event_dev(int eid)
{
	int fd;

	FMTBUF(p, e, path, 50);
	p = fmtstr(p, e, "/dev/input/event");
	p = fmtint(p, e, eid);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);

	return fd;
}

static int open_named_dev(char* name)
{
	int pl;
	char* p = name;
	int eid;

	if((pl = prefixed(p, "input")) > 0)
		fail("need eventN not inputN device", NULL, 0);

	if((pl = prefixed(p, "/dev/input/event")) > 0)
		p += pl;
	else if((pl = prefixed(p, "event")) > 0)
		p += pl;

	if(!(p = parseint(p, &eid)) || *p)
		fail("bad device name", name, 0);

	return open_event_dev(eid);
}

void event_monitor(int argc, char** args)
{
	char buf[256];
	int rd;

	if(argc < 1)
		fail("device name required", NULL, 0);
	if(argc > 1)
		fail("too many arguments", NULL, 0);

	int fd = open_named_dev(args[0]);

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		char* ptr = buf;
		char* end = buf + rd;

		while(ptr < end) {
			dump_event((struct event*) ptr);
			ptr += sizeof(struct event);
		}
	}
}

/* Entry point */

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	argc -= i;
	argv += i;

	if(opts & OPT_r)
		event_monitor(argc, argv);
	else if(argc)
		list_args(argc, argv, opts | OPT_a);
	else
		list_all_inputs(opts);

	return 0;
}
