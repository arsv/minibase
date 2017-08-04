#include <bits/types.h>
#include <printf.h>
#include <string.h>
#include <util.h>

/* So this is how qsort is used in minibase: the elements being sorted
   are usually pointers, and in all cases small enough to put onto stack;
   n is usually reasonably small, but cmp is often relatively expensive,
   and pivot-equal entries are to be expected.

   So this basically calls for careful 3-way partitioning
   and some special cases with shorter handling.
 
   The code below is loosely based on qsort from dietlibc, which in turn
   pulls from http://www.cs.princeton.edu/~rs/talks/QuicksortIsOptimal.pdf
   It's not a faithful implementation though, if it's wrong it's probably
   my fault. */

typedef void (*qexch)(void* a, void* b, size_t sz);

static void exch_long(long* a, long* b, size_t _)
{
	long t = *a;
	*a = *b;
	*b = t;
}

static void exch_int(int* a, int* b, size_t _)
{
	int t = *a;
	*a = *b;
	*b = t;
}

static void exch_any(void* a, void* b, size_t sz)
{
	char t[sz];
	memcpy(t, a, sz);
	memcpy(a, b, sz);
	memcpy(b, t, sz);
} 

static void* pick_pivot(void* S, void* E, size_t size)
{
	return S + size*((E - S)/size/2);
}

/* Bentley-McIlroy 3-way partitioning:

          eq eq lt lt ?? ?? ?? gt gt eq eq.

   Equal element are then swapped to the center. */

static void srec(void* S, void* E, size_t size, qcmp cmp, qexch exch, long opts);

static void sort(void* S, void* E, size_t size, qcmp cmp, qexch exch, long opts)
{
	void* pv = pick_pivot(S, E, size);

	void* le = S;
	void* re = E - size;
	void* ll = le;
	void* rr = re;

	exch(re, pv, size);  /* move pv to the back */
	pv = re; re -= size;

	void* lp = le;
	void* rp = re;
	int c;

	/* ll       le       lp                               pv */
	/* eq eq eq lt lt lt ?? ?? ?? ?? ?? gt gt gt eq eq eq eq */
	/*                               rp       re          rr */

	while(1) {
		while(lp < rp && (c = cmp(lp, pv, opts)) <= 0) {
			if(c == 0) {
				exch(le, lp, size);
				le += size;
			}
			lp += size;
		}
		while(rp > lp && (c = cmp(rp, pv, opts)) >= 0) {
			if(c == 0) {
				exch(re, rp, size);
				re -= size;
			}
			rp -= size;
		}
		if(lp == rp)
			break;

		exch(lp, rp, size);
		
		lp += size; if(rp > lp) rp -= size;
	}

	/* ll       le          lp                   pv */
	/* eq eq eq lt lt lt lt gt gt gt gt gt eq eq eq */
	/*                      rp          re       rr */

	if(cmp(lp, pv, opts) > 0)
		lp -= size;
	if(cmp(rp, pv, opts) < 0)
		rp += size;

	/* ll       le       lp                      pv */
	/* eq eq eq lt lt lt lt gt gt gt gt gt eq eq eq */
	/*                      rp          re       rr */
	
	for(pv = ll; pv < le;) {     /* pv is no longer pivot! */
		exch(pv, lp, size);
		lp -= size;
		pv += size;
	}
	for(pv = rr; pv > re;) {
		exch(pv, rp, size);
		pv -= size;
		rp += size;
	}

	/* ll       lp                               pv */
	/* lt lt lt lt eq eq eq eq eq eq gt gt gt gt gt */
	/*          le                   rp re       rr */

	if(lp > ll)
		srec(ll, lp + size, size, cmp, exch, opts);
	if(rp < rr)
		srec(rp, rr + size, size, cmp, exch, opts);
}

/* Special handling for low-n cases */

static void sort2(void* S, size_t size, qcmp cmp, qexch exch, long opts)
{
	void* a0 = S;
	void* a1 = S + size;

	if(cmp(a0, a1, opts) > 0)
		exch(a0, a1, size);
}

static void sort3(void* S, size_t size, qcmp cmp, qexch exch, long opts)
{
	void* a0 = S;
	void* a1 = a0 + size;
	void* a2 = a1 + size;

	if(cmp(a0, a1, opts) > 0)
		exch(a0, a1, size);
	if(cmp(a1, a2, opts) > 0)
		exch(a1, a2, size);
	if(cmp(a0, a1, opts) > 0)
		exch(a0, a1, size);
}

static void srec(void* S, void* E, size_t size, qcmp cmp, qexch exch, long opts)
{
	size_t len = E - S;
	size_t sz1 = size;
	size_t sz2 = sz1 + size;
	size_t sz3 = sz2 + size;

	if(len <= sz1)
		return;
	if(len == sz2)
		return sort2(S, size, cmp, exch, opts);
	if(len == sz3)
		return sort3(S, size, cmp, exch, opts);

	return sort(S, E, size, cmp, exch, opts);
}

void qsort(void* base, size_t n, size_t size, qcmp cmp, long opts)
{
	qexch exch;
	
	if(size == sizeof(long))
		exch = (qexch) exch_long;
	else if(size == sizeof(int))
		exch = (qexch) exch_int;
	else
		exch = exch_any;

	srec(base, base + n*size, size, cmp, exch, opts);
}
