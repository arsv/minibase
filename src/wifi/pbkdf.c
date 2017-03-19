#include <bits/ints.h>
#include <sys/write.h>
#include <sys/_exit.h>
#include <format.h>
#include <string.h>

#include "pbkdf2_sha1.h"

/* For development use only! Takes passphrase as argument,
   likely leaving it in shell history. */

static void die(const char* msg, const char* arg)
{
	int msglen = strlen(msg);
	int arglen = arg ? strlen(arg) : 0;
	char buf[msglen+arglen+2];

	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	};

	*p++ = '\n';

	syswrite(STDERR, buf, p - buf);

	_exit(0xFF);
}

static void dump_in_hex(uint8_t* psk, int len)
{
	char buf[2*len+1];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	for(int i = 0; i < len; i++)
		p = fmtbyte(p, e, psk[i]);

	*p++ = '\n';

	syswrite(STDOUT, buf, p - buf);
}

int main(int argc, char** argv)
{
	uint8_t psk[32];

	if(argc < 3)
		die("too few arguments", NULL);
	if(argc > 3)
		die("too many arguments", NULL);

	char* salt = argv[1];
	int saltlen = strlen(salt);

	char* pass = argv[2];
	int passlen = strlen(pass);

	pbkdf2_sha1(psk, sizeof(psk), pass, passlen, salt, saltlen, 4096);
	dump_in_hex(psk, sizeof(psk));

	return 0;
}
