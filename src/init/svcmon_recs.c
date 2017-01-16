#include <bits/socket.h>
#include <sys/mmap.h>
#include <sys/munmap.h>
#include <sys/read.h>

#include <null.h>
#include <string.h>
#include <format.h>

#include "svcmon.h"

struct svcrec recs[MAXRECS];
struct pollfd pfds[MAXRECS+1];

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

void flushrec(struct svcrec* rc)
{
	if(!rc->ring) return;

	sysmunmap(rc->ring, RINGSIZE);
}

void droprec(struct svcrec* rc)
{
	int i = recindex(rc);

	setpollfd(i, -1);
	flushrec(rc);
	memset(rc, 0, sizeof(*rc));

	if(i == gg.nr-1) gg.nr--;
}

static int mmapring(struct svcrec* rc)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	long ret = sysmmap(NULL, RINGSIZE, prot, flags, -1, 0);

	if(MMAPERROR(ret)) {
		return 0;
	} else {
		rc->ring = (char*) ret;
		rc->head = 0;
		rc->tail = 0;
		return 1;
	}
}

static void readring(struct svcrec* rc, int fd)
{
	char* start;
	int avail;

	if(rc->tail < RINGSIZE) {
		start = rc->ring + rc->tail;
		avail = RINGSIZE - rc->tail;
	} else {
		start = rc->ring;
		avail = RINGSIZE;
		rc->tail = 0;
	};

	int rd = sysread(fd, start, avail);

	if(rd < 0) return;

	if(rc->head > rc->tail && rc->head < rc->tail + rd)
		rc->head = (rc->tail + rd) % RINGSIZE;

	rc->tail = (rc->tail + rd) % RINGSIZE;
}

void bufoutput(int fd, int i)
{
	struct svcrec* rc = &recs[i-1];

	if(empty(rc))
		goto drop;

	if(!rc->ring && !mmapring(rc))
		goto drop;

	readring(rc, fd);
	return;
drop:
	setpollfd(i, -1);
}
