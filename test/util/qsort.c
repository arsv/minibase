#include <util.h>
#include <format.h>

static int intcmp(const void* a, const void* b, long _)
{
	int va = *((int*)a);
	int vb = *((int*)b);

	if(va < vb)
		return -1;
	if(va > vb)
		return 1;

	return 0;
}

static char* format_tag(char* p, char* e, const char* file, int line)
{
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");
	return p;
}

static char* put_array(char* p, char* e, int* a, int n)
{
	int i;

	p = fmtstr(p, e, " { ");

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, i ? ", " : "");
		p = fmtint(p, e, a[i]);
	}

	p = fmtstr(p, e, " }");

	return p;
}

static void end_format(char* s, char* p)
{
	*p++ = '\n';
	writeall(STDERR, s, p - s);
}

static int error(char* s, char* p, char* e, char* msg)
{
	p = fmtstr(p, e, "ERROR ");
	p = fmtstr(p, e, msg);
	end_format(s, p);

	return -1;
}

static int failure(char* s, char* p, char* e, int* a, int* b, int n)
{
	char* q = p;

	p = fmtstr(p, e, "FAIL");
	p = put_array(p, e, a, n);
	end_format(s, p);

	p = fmtstr(q, e, "    ");
	p = put_array(p, e, b, n);
	end_format(s, p);

	return 1;
}

static int success(char* s, char* p, char* e, int* a, int n)
{
	p = fmtstr(p, e, "OK");
	p = put_array(p, e, a, n);
	end_format(s, p);

	return 0;
}

static int compare(int* a, int* b, int n)
{
	int i;

	for(i = 0; i < n; i++)
		if(a[i] != b[i])
			return -1;

	return 0;
}

static int test(char* file, int line, int* a, int na, int* b, int nb)
{
	char buf[200];
	char* s = buf;
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = format_tag(p, e, file, line);

	if(na != nb)
		return error(s, p, e, "inconsistent array sizes");

	qsort(a, na, sizeof(*a), intcmp, 0);

	if(compare(a, b, na))
		return failure(s, p, e, a, b, na);

	return success(s, p, e, a, na);
}

#define q(...) { __VA_ARGS__ }

#define TEST(a, s) \
{\
	int A[] = a; \
	int S[] = s; \
	ret |= test(__FILE__, __LINE__, A, ARRAY_SIZE(A), S, ARRAY_SIZE(S)); \
}

int main(void)
{
	int ret = 0;

	TEST(q(), q());

	TEST(q(1, 0), q(0, 1));

	TEST(q(2, 1, 0), q(0, 1, 2));

	TEST(q( 1, 6, 5, 3, 4, 2, 7, 0 ),
	     q( 0, 1, 2, 3, 4, 5, 6, 7 ));

	TEST(q( 0, 1, 2, 3, 4, 5, 6, 7, 8 ),
	     q( 0, 1, 2, 3, 4, 5, 6, 7, 8 ));

	TEST(q( 8, 7, 6, 5, 4, 3, 2, 1, 0 ),
	     q( 0, 1, 2, 3, 4, 5, 6, 7, 8 ));

	TEST(q( 0, 1, 2, 3, 3, 3, 4, 5, 6 ),
	     q( 0, 1, 2, 3, 3, 3, 4, 5, 6 ));

	TEST(q( 3, 1, 3, 0, 6, 2, 4, 5, 3 ),
	     q( 0, 1, 2, 3, 3, 3, 4, 5, 6 ));

	TEST(q( 1, 1, 1, 1, 1, 1, 1, 1, 1 ),
	     q( 1, 1, 1, 1, 1, 1, 1, 1, 1 ));

	return ret;
}
