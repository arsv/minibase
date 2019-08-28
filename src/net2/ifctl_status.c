#include <format.h>
#include <string.h>
#include <nlusctl.h>
#include <output.h>
#include <util.h>
#include <main.h>

#include "ifctl.h"
#include "common.h"

/* Link list output */

static int count_links(MSG)
{
	struct ucattr* at;
	int count = 0;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(uc_is_nest(at, ATTR_LINK))
			count++;

	return count;
}

static void fill_links(MSG, struct ucattr** idx, int n)
{
	struct ucattr* at;
	int i = 0;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(i >= n)
			break;
		else if(!uc_is_nest(at, ATTR_LINK))
			continue;
		else
			idx[i++] = at;
}

static char* fmt_flags(char* p, char* e, struct ucattr* at)
{
	int* pf = uc_sub_int(at, ATTR_FLAGS);

	if(!pf) return p;

	int flags = *pf;

	if(flags & IF_FLAG_CARRIER)
		p = fmtstr(p, e, ", carrier");
	if(flags & IF_FLAG_AUTO_DHCP) {
		if(flags & IF_FLAG_DHCP_ONCE)
			p = fmtstr(p, e, ", dhcp-once");
		else
			p = fmtstr(p, e, ", auto-dhcp");
	} if(flags & IF_FLAG_FAILED) {
		p = fmtstr(p, e, ", failed");
	}

	if((flags & IF_FLAG_RUNNING)) {
		int what = flags & IF_STATE_MASK;

		p = fmtstr(p, e, ", running ");
		if(what == IF_STATE_IDEF)
			p = fmtstr(p, e, "identify");
		else if(what == IF_STATE_MODE)
			p = fmtstr(p, e, "mode");
		else if(what == IF_STATE_STOP)
			p = fmtstr(p, e, "stop");
		else {
			p = fmtstr(p, e, "0x");
			p = fmthex(p, e, what);
		}
	}

	return p;
}

static char* fmt_link(char* p, char* e, struct ucattr* at)
{
	int* ifi = uc_sub_int(at, ATTR_IFI);
	char* name = uc_sub_str(at, ATTR_NAME);
	char* mode = uc_sub_str(at, ATTR_MODE);

	if(!ifi || !name)
		return p;

	p = fmtint(p, e, *ifi);
	p = fmtstr(p, e, " ");

	if(name) {
		p = fmtstr(p, e, name);
	} else {
		p = fmtstr(p, e, "(unnamed)");
	}
	if(mode) {
		p = fmtstr(p, e, " mode ");
		p = fmtstr(p, e, mode);
	}

	p = fmt_flags(p, e, at);
	p = fmtstr(p, e, "\n");

	return p;
}

void dump_status(CTX, MSG)
{
	int i, n = count_links(msg);
	struct ucattr* idx[n];

	fill_links(msg, idx, n);

	FMTBUF(p, e, buf, 2048);

	for(i = 0; i < n; i++)
		p = fmt_link(p, e, idx[i]);

	FMTEND(p, e);

	writeall(STDOUT, buf, p - buf);
}
