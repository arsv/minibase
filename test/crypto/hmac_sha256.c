#include <crypto/sha256.h>
#include <string.h>
#include <printf.h>
#include <util.h>

/* Tests from RFC 4868 */

struct test {
	int inlen;
	char* input;
	int keylen;
	uint8_t key[64];
	uint8_t hash[32];
} tests[] = {
	/* Test case 1 */
	{ 8, "Hi There", 32,
	      { 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
	        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
	        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
	        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b },
	      { 0x19, 0x8a, 0x60, 0x7e, 0xb4, 0x4b, 0xfb, 0xc6,
	        0x99, 0x03, 0xa0, 0xf1, 0xcf, 0x2b, 0xbd, 0xc5,
	        0xba, 0x0a, 0xa3, 0xf3, 0xd9, 0xae, 0x3c, 0x1c,
	        0x7a, 0x3b, 0x16, 0x96, 0xa0, 0xb6, 0x8c, 0xf7,
	      } },
	{ 0, NULL, 0, { 0 }, { 0 } }
};

void dump(uint8_t hash[20])
{
	for(int i = 0; i < 20; i++)
		tracef("%02X ", hash[i]);
	tracef("\n");
}

int printable(char* msg)
{
	char* p;

	for(p = msg; *p; p++)
		if(*p & 0x80 || *p < 0x20)
			return 0;

	return 1;
}

static void test(struct test* tp)
{
	int inlen = tp->inlen;
	char* input = tp->input;
	int klen = tp->keylen;
	uint8_t* key = tp->key;
	uint8_t* hash = tp->hash;
	uint8_t temp[32];

	hmac_sha256(temp, key, klen, input, inlen);

	if(!memcmp(hash, temp, 20))
		return;

	tracef("FAIL %s\n", tp->input);
	dump(hash);
	dump(temp);

	_exit(0xFF);
}

int main(void)
{
	struct test* tp;

	for(tp = tests; tp->input; tp++)
		test(tp);

	return 0;
}
