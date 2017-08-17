#include <sys/file.h>
#include <string.h>
#include <fail.h>
#include "keytool.h"

int ask(char* tag, char* buf, int len)
{
	int rd;

	sys_write(STDOUT, tag, strlen(tag));

	if((rd = sys_read(STDIN, buf, len)) < 0)
		fail("read", "stdin", rd);
	if((rd >= len))
		fail("passphrase too long", NULL, 0);

	if(rd && buf[rd-1] == '\n')
		rd--;
	if(rd == 0)
		fail("empty passphrase", NULL, 0);

	buf[rd] = '\0';

	return rd;
}
