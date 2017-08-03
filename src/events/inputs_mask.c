#include <bits/input.h>
#include <sys/ioctl.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <null.h>

#include "inputs.h"

static char* getname(const struct ev* ev, int k)
{
	if(k < 0 || k >= ev->count)
		return NULL;
	return (char*)ev->names[k];
}

static int hascode(char* bits, int len, int code)
{
	if(code < 0)
		return 0;
	if(code / 8 >= len)
		return 0;

	return bits[code/8] & (1 << (code % 8));
}

static int numdigits(int x)
{
	int num = 1;

	while(x >= 10) { num++; x /= 10; }

	return num;
}

static const char indent1[] = "  ";
static const char indent2[] = "       ";

static char* start_line(char* s, char* p, char* e, const struct ev* ev)
{
	p = fmtstr(p, e, indent1);
	p = fmtstr(p, e, ev->tag);
	p = fmtstr(p, e, ":");
	return p;
}

static char* put_bit(char* s, char* p, char* e, const struct ev* ev, int k)
{
	char* desc = getname(ev, k);
	int skiplen = 3 + numdigits(k) + (desc ? strlen(desc) : 0);

	if(p + skiplen >= e) {
		*p++ = '\n';
		writeout(s, p - s);
		p = fmtstr(s, e, indent2);
	} else {
		p = fmtstr(p, e, " ");
	}

	p = fmtint(p, e, k);
	
	if(desc) {
		p = fmtstr(p, e, "/");
		p = fmtstr(p, e, desc);
	}

	return p;
}

static void finish_line(char* s, char* p, char* e)
{
	*p++ = '\n';
	writeout(s, p - s);
}

static void dump_bits(const struct ev* ev, char* bits, int size)
{
	char line[70+1];
	char* s = line;
	char* p = line;
	char* e = line + sizeof(line) - 1;
	int i, b, k;

	p = start_line(s, p, e, ev);

	for(i = 0; i < size; i++) {
		if(!bits[i])
			continue;
		for(b = 0; b < 8; b++) {
			if(!hascode(bits, size, (k = 8*i + b)))
				continue;
			p = put_bit(s, p, e, ev, k);
		}
	}

	finish_line(s, p, e);
}

static void query(int fd, const struct ev* ev)
{
	int size = ev->size;
	int type = ev->type;
	char bits[size];
	int ret;

	memzero(bits, size);

	if((ret = sys_ioctl(fd, EVIOCGBIT(type, size), bits)) < 0)
		return;
	if(!nonzero(bits, size))
		return;

	dump_bits(ev, bits, size);
}

void query_event_bits(int fd)
{
	query(fd, &ev_led);
	query(fd, &ev_rel);
	query(fd, &ev_abs);
	query(fd, &ev_sw);
	query(fd, &ev_key);
}
