#include <format.h>
#include <string.h>

void nl_hexdump(char* inbuf, int inlen)
{
	int spacelen = 4 + 3 + 3 + 2;
	int hexlen = 16*3;
	int charlen = 16;
	int total = spacelen + hexlen + charlen;
	static const char digits[] = "0123456789ABCDEF";

	char linebuf[total];
	char* e = linebuf + sizeof(linebuf) - 1;
	*e = '\0';

	int lines = (inlen/16) + (inlen & 15 ? 1 : 0);

	int l, c, i;
	char* p;

	for(l = 0; l < lines; l++) {
		memset(linebuf, ' ', e - linebuf);
		for(c = 0; c < 16; c++) {
			i = l*16 + c;
			if(i >= inlen) break;

			uint8_t x = inbuf[i];

			p = linebuf + 4 + 3*c + (c >= 8 ? 2 : 0);
			p[0] = digits[((x >> 4) & 15)];
			p[1] = digits[((x >> 0) & 15)];
		}

		for(c = 0; c < 16; c++) {
			i = l*16 + c;
			if(i >= inlen) break;

			uint8_t x = inbuf[i];

			p = linebuf + 4 + 16*3 + 4 + c;
			*p = (x >= 0x20 && x < 0x7F) ? x : '.';
		}
		eprintf("%s\n", linebuf);
	}
}
