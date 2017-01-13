#include <null.h>
#include <string.h>
#include "init.h"

int nrecs = 0;
struct initrec recs[MAXRECS];

static int empty(struct initrec* rc)
{
	return !rc->name[0];
}

struct initrec* findrec(char* name)
{
	int i;

	for(i = 0; i < nrecs; i++)
		if(empty(&recs[i]))
			continue;
		else if(!strcmp(recs[i].name, name))
			return &recs[i];

	return NULL;
}

struct initrec* makerec(void)
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

static struct initrec* advance_to_nonempty(struct initrec* rc)
{
	while(rc < &recs[nrecs] && empty(rc))
		rc++;
	if(rc < &recs[nrecs])
		return rc;
	return NULL;
}

struct initrec* firstrec(void)
{
	return advance_to_nonempty(&recs[0]);
}

struct initrec* nextrec(struct initrec* rc)
{
	if((void*)rc < (void*)recs)
		return NULL;
	return advance_to_nonempty(rc+1);
}

void droprec(struct initrec* rc)
{
	memset(rc, 0, sizeof(*rc));
}
