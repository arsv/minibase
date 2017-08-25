#include <string.h>
#include <format.h>
#include <util.h>

#include "super.h"

struct proc procs[NPROCS];
struct conn conns[NCONNS];
int nprocs;
int nconns;

static int empty(struct proc* rc)
{
	return !rc->name[0];
}

static void* grab_slot(void* slots, int* count, int total, int size)
{
	void* ptr = slots + size*(*count);
	void* end = slots + size*total;
	void* p;

	for(p = slots; p < ptr; p += size)
		if(!nonzero(p, size))
			break;
	if(p < ptr)
		return p;
	if(p >= end)
		return NULL;

	*count += 1;
	return p;
}

static void free_slot(void* slots, int* count, int size, void* p)
{
	memzero(p, size);

	while(*count > 0) {
		p = slots + size*(*count - 1);

		if(nonzero(p, size))
			break;

		*count -= 1;
	}
}

struct proc* grab_proc_slot(void)
{
	return grab_slot(procs, &nprocs, NPROCS, sizeof(*procs));
}

void free_proc_slot(struct proc* rc)
{
	free_slot(procs, &nprocs, sizeof(*rc), rc);
}

struct conn* grab_conn_slot(void)
{
	return grab_slot(conns, &nconns, NCONNS, sizeof(*conns));
}

static struct proc* advance_to_nonempty(struct proc* rc)
{
	struct proc* end = procs + nprocs;

	while(rc < end && empty(rc))
		rc++;
	if(rc < end)
		return rc;
	return NULL;
}

struct proc* firstrec(void)
{
	return advance_to_nonempty(&procs[0]);
}

struct proc* nextrec(struct proc* rc)
{
	if((void*)rc < (void*)procs)
		return NULL;

	return advance_to_nonempty(rc+1);
}

struct proc* find_by_name(char* name)
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		if(!strcmp(rc->name, name))
			return rc;

	return NULL;
}

struct proc* find_by_pid(int pid)
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		if(rc->pid == pid)
			return rc;

	return NULL;
}
