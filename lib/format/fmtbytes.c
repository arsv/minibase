#include <bits/ints.h>
#include <format.h>

char* fmtbytes(char* p, char* e, void* data, size_t len)
{
	uint8_t* val = data;
	uint8_t* end = data + len;

	for(; val < end; val++)
		p = fmtbyte(p, e, *val);

	return p;
}
