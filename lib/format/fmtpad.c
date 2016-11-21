#include <format.h>

char* fmtpad(char* p, char* e, int width, char* q)
{
	int qplen = q - p;
	int shift = width - qplen;

	if(shift <= 0) return q;

	char* z;

	for(z = q + shift - 1; z >= p + shift; z--)
		if(z < e)
			*z = *(z - shift);
	for(; z >= p; z--)
		*z = ' ';

	return q + shift;
}
