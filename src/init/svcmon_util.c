#include <bits/socket.h>
#include <sys/write.h>
#include <sys/brk.h>

#include <null.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "svcmon.h"

struct svcrec recs[MAXRECS];

static int empty(struct svcrec* rc)
{
	return !rc->name[0];
}

struct svcrec* findrec(char* name)
{
	int i;

	for(i = 0; i < gg.nr; i++)
		if(empty(&recs[i]))
			continue;
		else if(!strcmp(recs[i].name, name))
			return &recs[i];

	return NULL;
}

struct svcrec* findpid(int pid)
{
	int i;

	for(i = 0; i < gg.nr; i++)
		if(recs[i].pid == pid)
			return &recs[i];

	return NULL;
}

struct svcrec* makerec(void)
{
	int i;

	for(i = 0; i < gg.nr; i++)
		if(empty(&recs[i]))
			break;

	if(i == gg.nr && gg.nr < MAXRECS)
		gg.nr++;

	if(i >= gg.nr)
		return NULL;

	return &recs[i];
}

static struct svcrec* advance_to_nonempty(struct svcrec* rc)
{
	while(rc < &recs[gg.nr] && empty(rc))
		rc++;
	if(rc < &recs[gg.nr])
		return rc;
	return NULL;
}

struct svcrec* firstrec(void)
{
	return advance_to_nonempty(&recs[0]);
}

struct svcrec* nextrec(struct svcrec* rc)
{
	if((void*)rc < (void*)recs)
		return NULL;

	return advance_to_nonempty(rc+1);
}

int recindex(struct svcrec* rc)
{
	return rc - recs;
}

void droprec(struct svcrec* rc)
{
	int i = recindex(rc);

	setpollfd(rc, -1);
	flushring(rc);
	memset(rc, 0, sizeof(*rc));

	if(i == gg.nr-1) gg.nr--;
}

/* Heap, used only for large buffers in load_dir_ents() and dumpstate() */

static char* brk;
static char* ptr;
static char* end;

void setbrk(void)
{
	brk = (char*)sysbrk(NULL);
	ptr = end = brk;
}

char* alloc(int len)
{
	char* old = ptr;
	char* req = old + len;

	if(req <= end)
		goto done;

	end = (char*)sysbrk(req);

	if(req > end) {
		report("out of memory", NULL, 0);
		return NULL;
	}
done:
	ptr += len;
	return old;
}

void afree(void)
{
	end = ptr = (char*)sysbrk(brk);
}

/* Error output */

static const char tag[] = "svcmon";
static char warnbuf[200];

void report(char* msg, char* arg, int err)
{
	char* p = warnbuf;
	char* e = warnbuf + sizeof(warnbuf) - 1;

	if(gg.outfd <= STDERR) {
		p = fmtstr(p, e, tag);
		p = fmtstr(p, e, ": ");
	};

	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	};

	if(err) {
		p = fmtstr(p, e, ": ");
		p = fmtint(p, e, err);
	};

	*p++ = '\n';

	writeall(gg.outfd, warnbuf, p - warnbuf);
}

void reprec(struct svcrec* rc, char* msg)
{
	char* p = warnbuf;
	char* e = warnbuf + sizeof(warnbuf) - 1;

	p = fmtstr(p, e, rc->name);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);

	*p++ = '\n';

	writeall(gg.outfd, warnbuf, p - warnbuf);
}
