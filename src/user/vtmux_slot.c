#include <null.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "vtmux.h"

struct term terms[NTERMS];
struct conn conns[NCONNS];
struct mdev mdevs[NMDEVS];

int nconns;
int nterms;
int nmdevs;

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

struct term* grab_term_slot(void)
{
	return grab_slot(terms, &nterms, NTERMS, sizeof(*terms));
}

struct conn* grab_conn_slot(void)
{
	return grab_slot(conns, &nconns, NCONNS, sizeof(*conns));
}

struct mdev* grab_mdev_slot(void)
{
	return grab_slot(mdevs, &nmdevs, NMDEVS, sizeof(*mdevs));
}

void free_term_slot(struct term* vt)
{
	return free_slot(terms, &nterms, sizeof(*vt), vt);
}

void free_mdev_slot(struct mdev* md)
{
	return free_slot(mdevs, &nmdevs, sizeof(*md), md);
}

struct term* find_term_by_pid(int pid)
{
	struct term* vt;

	for(vt = terms; vt < terms + nterms; vt++)
		if(vt->pid == pid)
			return vt;

	return NULL;
}

struct term* find_term_by_tty(int tty)
{
	struct term* vt;

	for(vt = terms; vt < terms + nterms; vt++)
		if(vt->tty == tty)
			return vt;

	return NULL;
}
