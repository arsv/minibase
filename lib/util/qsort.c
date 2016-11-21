#include <bits/types.h>
#include <util.h>

/* Code from dietlibc, slightly reformatted. */

typedef long ssize_t;

#define a(i) (base + (i)*size)

static void exch(char* base, size_t size, size_t i, size_t j)
{
	char* pi = a(i);
	char* pj = a(j);

	for(; size > 0; size--) {
		char z = *pi;
		*pi++ = *pj;
		*pj++ = z;
	}
}

/* Quicksort with 3-way partitioning, ala Sedgewick */
/* Blame him for the scary variable names */
/* http://www.cs.princeton.edu/~rs/talks/QuicksortIsOptimal.pdf */
static void quicksort(char* base, size_t size, ssize_t l, ssize_t r, qcmp compar, long d)
{
	ssize_t i=l-1, j=r, p=l-1, q=r, k;
	char* v = a(r);

	for (;;) {
		while(++i != r && compar(a(i), v, d) < 0)
			;
		while(compar(v, a(--j), d) < 0)
			if(j == l)
				break;
		if (i >= j)
			break;

		exch(base, size, i, j);

		if(compar(a(i), v, d) == 0)
			exch(base, size, ++p, i);
		if(compar(v, a(j), d) == 0)
			exch(base, size, j, --q);
	}

	exch(base, size, i, r);
	j = i-1; ++i;

	for(k = l;   k < p; k++, j--)
		exch(base, size, k, j);
	for(k = r-1; k > q; k--, i++)
		exch(base, size, i, k);

	if(j > l)
		quicksort(base, size, l, j, compar, d);
	if(r > i)
		quicksort(base, size, i, r, compar, d);
}

void qsort(void* base, size_t nmemb, size_t size, qcmp compar, long d)
{
	/* XXX: int overflow checks; only needed for (l-1) and similar stuff? */

	if(nmemb <= 1)
		return;

	quicksort(base, size, 0, nmemb-1, compar, d);
}
