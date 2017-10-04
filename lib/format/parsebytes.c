#include <bits/ints.h>
#include <format.h>

char* parsebytes(char* p, byte* dst, uint len)
{
	byte* q = dst;

	while(len-- > 0 && p)
		p = parsebyte(p, q++);

	return p;
}
