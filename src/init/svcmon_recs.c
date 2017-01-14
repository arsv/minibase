#include <null.h>
#include <string.h>
#include "svcmon.h"

int nrecs = 0;
struct svcrec recs[MAXRECS];

static int empty(struct svcrec* rc)
{
	return !rc->name[0];
}

struct svcrec* findrec(char* name)
{
	int i;

	for(i = 0; i < nrecs; i++)
		if(empty(&recs[i]))
			continue;
		else if(!strcmp(recs[i].name, name))
			return &recs[i];

	return NULL;
}

struct svcrec* makerec(void)
{
	int i;

	for(i = 0; i < nrecs; i++)
		if(empty(&recs[i]))
			break;

	if(i == nrecs && nrecs < MAXRECS)
		nrecs++;
	if(i >= nrecs)
		return NULL;

	return &recs[i];
}

static struct svcrec* advance_to_nonempty(struct svcrec* rc)
{
	while(rc < &recs[nrecs] && empty(rc))
		rc++;
	if(rc < &recs[nrecs])
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

void droprec(struct svcrec* rc)
{
	memset(rc, 0, sizeof(*rc));
}
