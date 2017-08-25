#include <sys/file.h>
#include <sys/mman.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <time.h>
#include <util.h>

#include "common.h"

ERRTAG("logcat");

#define TAGSPACE 13

#define OPTS "c"
#define OPT_c (1<<0)

struct mbuf {
	char* ptr;
	char* end;
};

static void mmap_whole(struct mbuf* mb, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	uint64_t size = st.size;

	long ptr = sys_mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if(mmap_error(ptr))
		fail("mmap", name, ptr);

	mb->ptr = (void*)ptr;
	mb->end = mb->ptr + size;
}

static int tagged(char* ls, char* le, char* tag, int tlen)
{
	if(le - ls < TAGSPACE + tlen + 1)
		return 0;
	if(strncmp(ls + TAGSPACE, tag, tlen))
		return 0;
	if(ls[TAGSPACE+tlen] != ':')
		return 0;

	return 1;
}

static char* color(char* p, char* e, int opts, int a, int b)
{
	if(opts & OPT_c)
		return p;

	p = fmtstr(p, e, "\033[");
	p = fmtint(p, e, a);
	p = fmtstr(p, e, ";");
	p = fmtint(p, e, b);
	p = fmtstr(p, e, "m");

	return p;
}

static char* reset(char* p, char* e, int opts)
{
	if(opts & OPT_c)
		return p;

	p = fmtstr(p, e, "\033[0m");

	return p;
}

static void outcolor(int opts, int a, int b)
{
	if(opts & OPT_c) return;

	FMTBUF(p, e, buf, 10);
	p = color(p, e, opts, a, b);
	FMTEND(p, e);

	writeout(buf, p - buf);
}

static void outreset(int opts)
{
	if(opts & OPT_c) return;

	writeout("\033[0m", 4);
}

static char* skip_prefix(char* ls, char* le)
{
	char* p;

	for(p = ls; p < le; p++)
		if(*p == ' ')
			break;
		else if(*p == ':')
			return p + 1;

	return NULL;
}

static void output(uint64_t ts, int prio, char* ls, char* le, int opts)
{
	struct timeval tv = { ts, 0 };
	struct tm tm;
	char* sep;

	tv2tm(&tv, &tm);

	FMTBUF(p, e, buf, 50);
	p = color(p, e, opts, 0, 32);
	p = fmttm(p, e, &tm);
	p = reset(p, e, opts);
	p = fmtstr(p, e, opts & OPT_c ? "  " : " ");
	FMTEND(p, e);

	writeout(buf, p - buf);

	if((sep = skip_prefix(ls, le))) {
		outcolor(opts, 0, 33);
		writeout(ls, sep - ls);
		outreset(opts);
		ls = sep;
	}

	if(prio < 2)
		outcolor(opts, 1, 31);
	else if(prio < 4)
		outcolor(opts, 1, 37);

	writeout(ls, le - ls);

	if(prio < 4)
		outreset(opts);

	writeout("\n", 1);
}

void format(char* ls, char* le, int opts)
{
	int ln = le - ls;
	int tl = TAGSPACE;
	int prio;
	char pref[tl+1];
	uint64_t ts;
	char* p;

	if(ln < TAGSPACE)
		return;

	memcpy(pref, ls, tl);
	pref[tl] = '\0';

	if(!(p = parseu64(pref, &ts)) || *p++ != ' ')
		return;
	if(*p < '0' || *p > '9')
		return;

	prio = *p - '0';

	output(ts, prio, ls + tl, le, opts);
}

void dump_logfile(char* name, char* tag, int opts)
{
	struct mbuf mb;
	char *ls, *le;
	int tlen = tag ? strlen(tag) : 0;

	mmap_whole(&mb, name);

	for(ls = mb.ptr; ls < mb.end; ls = le + 1) {
		le = strecbrk(ls, mb.end, '\n');

		if(le - ls < TAGSPACE)
			continue;
		if(tag && !tagged(ls, le, tag, tlen))
			continue;

		format(ls, le, opts);
	}
}

int main(int argc, char** argv)
{
	int i = 1;
	char* tag = NULL;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		tag = argv[i++];
	if(i < argc)
		fail("too many arguments", NULL, 0);

	dump_logfile(VARLOG, tag, opts);

	flushout();

	return 0;
}
