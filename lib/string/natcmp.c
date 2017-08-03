#include <string.h>

static int isnumeric(int c)
{
	return (c >= '0' && c <= '9') ? 1 : 0;
}

static unsigned long numrun(unsigned const char* p)
{
	unsigned const char* q = p;

	while(*q && isnumeric(*q)) q++;

	return q - p;
}

int natcmp(const char* a, const char* b)
{
	const unsigned char* pa = (const unsigned char*) a;
	const unsigned char* pb = (const unsigned char*) b;

	while(*pa && *pb) {
		if(*pa != *pb)
			break;
		pa++; pb++;
	}

	unsigned long na = numrun(pa);
	unsigned long nb = numrun(pb);

	if(na && nb && na != nb)
		return na - nb;

	return *pa - *pb;
}
