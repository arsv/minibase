#include <util.h>
#include <format.h>

static long cast(const void* ptr, long sz)
{
	if(sz == sizeof(long))
		return *((long*)ptr);
	if(sz == sizeof(int))
		return *((int*)ptr);
	if(sz == sizeof(char))
		return *((char*)ptr);
	return 0;
}

static int cmp(const void* a, const void* b, long sz)
{
	long va = cast(a, sz);
	long vb = cast(b, sz);

	if(va < vb)
		return -1;
	if(va > vb)
		return 1;

	return 0;
}

static char* put_array(char* p, char* e, void* a, size_t n, size_t sz)
{
	uint i;
	void* v = a;

	p = fmtstr(p, e, " { ");

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, i ? ", " : "");
		p = fmtlong(p, e, cast(v, sz));
		v += sz;
	}

	p = fmtstr(p, e, " }");

	return p;
}

static int check_order(void* a, size_t n, size_t sz)
{
	size_t i;
	void *u, *v = a;

	for(i = 1; i < n; i++)
		if(cmp(v, (u = v + sz), sz) > 0)
			return -1;
		else
			v = u;

	return 0;
}

static void test(char* file, int line, void* a, size_t n, size_t sz)
{
	qsortx(a, n, sz, cmp, sz);

	if(check_order(a, n, sz) == 0)
		return;

	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "FAIL");
	p = put_array(p, e, a, n, sz);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);

	_exit(0xFF);
}

#define TEST(type, ...) \
{\
	type A[] = { __VA_ARGS__ }; \
	test(__FILE__, __LINE__, A, ARRAY_SIZE(A), sizeof(*A)); \
}

int main(void)
{
	TEST(int);
	TEST(int, 1, 0);
	TEST(int, 2, 1, 0);
	TEST(int, 2, 3, 1, 0);

	TEST(int, 8, 4, 8, 1, 4, 1, 4, 1, 8);
	TEST(long, 8, 4, 8, 1, 4, 1, 4, 1, 8);

	TEST(int, 1, 6, 5, 3, 4, 2, 7, 0);
	TEST(int, 0, 1, 2, 3, 4, 5, 6, 7, 8);
	TEST(int, 8, 7, 6, 5, 4, 3, 2, 1, 0);

	TEST(char, 0, 1, 2, 3, 3, 3, 4, 5, 6);

	TEST(int, 3, 1, 3, 0, 6, 2, 4, 5, 3);
	TEST(int, 1, 1, 1, 1, 1, 1, 1, 1, 1);
	TEST(int, 1, 2, 2, 1, 1, 1, 1, 1, 1);
	TEST(int, 1, 2, 2, 1, 3, 1, 1, 1, 1);
	TEST(int, 1, 2, 2, 1, 0, 1, 1, 1, 1);

	TEST(int, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
	          10,  9,  8,  7,  6,  5,  4,  3,  2,  1);

	return 0;
}
