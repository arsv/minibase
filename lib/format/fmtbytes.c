#include <bits/ints.h>
#include <format.h>

char* fmtbytes(char* p, char* e, const void* data, uint len)
{
	const uint8_t* val = data;
	const uint8_t* end = data + len;

	for(; val < end; val++)
		p = fmtbyte(p, e, *val);

	return p;
}
