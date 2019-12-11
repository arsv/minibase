#include <bits/types.h>
#include <printf.h>
#include <string.h>
#include <util.h>

/* So this is how qsort is used in minibase: the elements being sorted
   are always pointers, and in all cases small enough to put onto stack;
   n is usually reasonably small, but cmp is often relatively expensive,
   and pivot-equal entries are to be expected.

   So this basically calls for careful 3-way partitioning
   and some special cases with shorter handling.

   The code below is loosely based on qsort from dietlibc, which in turn
   pulls from http://www.cs.princeton.edu/~rs/talks/QuicksortIsOptimal.pdf
   It's not a faithful implementation though, if it's wrong it's probably
   my fault. */

static inline void exch(void** a, void** b)
{
	void* t;
	 t = *a;
	*a = *b;
	*b = t;
}

static void** pick_pivot(void** S, void** E)
{
	return S + (E - S)/2;
}

/* Bentley-McIlroy 3-way partitioning:

          eq eq lt lt ?? ?? ?? gt gt eq eq.

   Equal element are then swapped to the center. */

static void srec(void** S, void** E, qcmp3 cmp, long opts);

static void sort(void** S, void** E, qcmp3 cmp, long opts)
{
	void** pv = pick_pivot(S, E);

	void** le = S;
	void** re = E - 1;
	void** ll = le;
	void** rr = re;

	exch(re, pv);  /* move pv to the back */
	pv = re--;

	void** lp = le;
	void** rp = re;
	int c;

	/* ll       le       lp                               pv */
	/* eq eq eq lt lt lt ?? ?? ?? ?? ?? gt gt gt eq eq eq eq */
	/*                               rp       re          rr */

	while(1) {
		while(lp < rp && (c = cmp(*lp, *pv, opts)) <= 0) {
			if(c == 0)
				exch(le++, lp);
			lp++;
		}
		while(rp > lp && (c = cmp(*rp, *pv, opts)) >= 0) {
			if(c == 0)
				exch(re--, rp);
			rp--;
		}
		if(lp == rp)
			break;

		exch(lp, rp);

		lp++; if(rp > lp) rp--;
	}

	/* ll       le          lp                   pv */
	/* eq eq eq lt lt lt lt gt gt gt gt gt eq eq eq */
	/*                      rp          re       rr */

	if(cmp(*lp, *pv, opts) > 0)
		lp--;
	if(cmp(*rp, *pv, opts) < 0)
		rp++;

	/* ll       le       lp                      pv */
	/* eq eq eq lt lt lt lt gt gt gt gt gt eq eq eq */
	/*                      rp          re       rr */

	for(pv = ll; pv < le;)      /* pv is no longer pivot! */
		exch(pv++, lp--);
	for(pv = rr; pv > re;)
		exch(pv--, rp++);

	/* ll       lp                               pv */
	/* lt lt lt lt eq eq eq eq eq eq gt gt gt gt gt */
	/*          le                   rp re       rr */

	if(lp > ll)
		srec(ll, lp + 1, cmp, opts);
	if(rp < rr)
		srec(rp, rr + 1, cmp, opts);
}

/* Special handling for low-n cases */

static void sort2(void** S, qcmp3 cmp, long opts)
{
	void** a0 = S + 0;
	void** a1 = S + 1;

	if(cmp(*a0, *a1, opts) > 0)
		exch(a0, a1);
}

static void sort3(void** S, qcmp3 cmp, long opts)
{
	void** a0 = S + 0;
	void** a1 = S + 1;
	void** a2 = S + 2;

	if(cmp(*a0, *a1, opts) > 0)
		exch(a0, a1);
	if(cmp(*a1, *a2, opts) > 0)
		exch(a1, a2);
	if(cmp(*a0, *a1, opts) > 0)
		exch(a0, a1);
}

static void srec(void** S, void** E, qcmp3 cmp, long opts)
{
	size_t len = E - S;

	if(len <= 1)
		return;
	if(len == 2)
		return sort2(S, cmp, opts);
	if(len == 3)
		return sort3(S, cmp, opts);

	return sort(S, E, cmp, opts);
}

void qsortx(void* ptrs, size_t n, qcmp3 cmp, long opts)
{
	void** S = ptrs;
	void** E = S + n;

	srec(S, E, cmp, opts);
}

static int cmp2to3(void* a, void* b, long arg)
{
	qcmp2 cmp = (qcmp2)arg;
	return cmp(a, b);
}

void qsortp(void* base, size_t n, qcmp2 cmp)
{
	return qsortx(base, n, cmp2to3, (long)cmp);
}
