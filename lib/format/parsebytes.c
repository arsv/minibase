#include <bits/ints.h>
#include <format.h>

char* parsebytes(char* p, uint8_t* dst, int len)
{
	uint8_t* q = dst;

	while(len-- > 0 && p)
		p = parsebyte(p, q++);

	return p;
}
