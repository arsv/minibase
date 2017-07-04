#include <sys/file.h>

#include <crypto/pbkdf2.h>
#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <heap.h>
#include <util.h>
#include <fail.h>

#include "common.h"
#include "wictl.h"

static void hexencode(char* dst, int dlen, uint8_t* psk, int plen)
{
	char* p = dst;
	char* e = dst + dlen - 1;
	int i;

	for(i = 0; i < plen; i++)
		p = fmtbyte(p, e, psk[i]);

	*p++ = '\0';
}

static void put_psk(CTX, uint8_t* ssid, int slen, char* pass, int plen)
{
	uint8_t psk[32];
	char strpsk[64+4];

	memset(psk, 0, sizeof(psk));

	pbkdf2_sha1(psk, sizeof(psk), pass, plen, ssid, slen, 4096);

	hexencode(strpsk, sizeof(strpsk), psk, sizeof(psk));

	uc_put_str(UC, ATTR_PSK, strpsk);
}

static int input_passphrase(char* buf, int len)
{
	int rd;
	char* prompt = "Passphrase: ";

	sys_write(STDOUT, prompt, strlen(prompt));
	rd = sys_read(STDIN, buf, len);

	if(rd >= len)
		fail("passphrase too long", NULL, 0);

	if(rd > 0 && buf[rd-1] == '\n')
		rd--;
	if(!rd)
		fail("empty passphrase rejected", NULL, 0);

	buf[rd] = '\0';

	return rd;
}

/* As written this only works with sane ssids and passphrases.
   Should be extended at some point to handle ssid escapes and
   multiline phrases. */

void put_psk_input(CTX, void* ssid, int slen)
{
	char buf[256];
	int len = input_passphrase(buf, sizeof(buf));

	put_psk(ctx, ssid, slen, buf, len);
}
