#include <util.h>
#include <format.h>

static const int numbers[] = {
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29
};

static int cmp(void* pa, void* pb)
{
	int a = *((int*)pa);
	int b = *((int*)pb);

	if(a < b)
		return -1;
	if(a > b)
		return 1;

	return 0;
}

static char* put_array(char* p, char* e, int* A[], int n)
{
	p = fmtstr(p, e, " { ");

	for(int i = 0; i < n; i++) {
		p = fmtstr(p, e, i ? ", " : "");
		p = fmtlong(p, e, *(A[i]));
	}

	p = fmtstr(p, e, " }");

	return p;
}

static void noreturn failure(char* file, int line, int* A[], int n)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, "FAIL");
	p = put_array(p, e, A, n);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);

	_exit(0xFF);
}

static int check_order(int* A[], int n)
{
	for(int i = 1; i < n; i++)
		if(cmp(A[i-1], A[i]) > 0)
			return -1;
	return 0;
}

static void test(char* file, int line, int idx[], int n)
{
	int* A[n];
	
	for(int i = 0; i < n; i++)
		A[i] = (int*)&numbers[idx[i]];

	qsortp(A, n, cmp);

	if(check_order(A, n) == 0)
		return;

	failure(file, line, A, n);
}

#define TEST(type, ...) \
{\
	int X[] = { __VA_ARGS__ }; \
	test(__FILE__, __LINE__, X, ARRAY_SIZE(X)); \
}

int main(void)
{
	TEST();

	TEST(1, 0);
	TEST(0, 1);

	TEST(1, 2, 3);
	TEST(1, 1, 1);
	TEST(3, 2, 1);

	TEST(1, 2, 3, 4);
	TEST(4, 3, 2, 1);
	TEST(1, 4, 2, 3);

	TEST(1, 2, 3, 4, 5);
	TEST(5, 4, 3, 2, 1);

	TEST(8, 4, 8, 1, 4, 1, 4, 1, 8);
	TEST(1, 6, 5, 3, 4, 2, 7, 0);
	TEST(0, 1, 2, 3, 4, 5, 6, 7, 8);
	TEST(8, 7, 6, 5, 4, 3, 2, 1, 0);
	TEST(3, 1, 3, 0, 6, 2, 4, 5, 3);
	TEST(1, 1, 1, 1, 1, 1, 1, 1, 1);
	TEST(1, 2, 2, 1, 1, 1, 1, 1, 1);
	TEST(1, 2, 2, 1, 3, 1, 1, 1, 1);
	TEST(1, 2, 2, 1, 0, 1, 1, 1, 1);

	TEST(20, 19, 18, 17, 16, 15, 14, 13, 12, 11,
	     10,  9,  8,  7,  6,  5,  4,  3,  2,  1);

	return 0;
}
