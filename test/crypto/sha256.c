#include <crypto/sha256.h>
#include <string.h>
#include <printf.h>
#include <util.h>

static void dump(char* tag, uint8_t hash[32])
{
	printf(" %s: ", tag);
	for(int i = 0; i < 32; i++)
		printf("%02X ", hash[i]);
	printf("\n");
}

static int printable(char* msg)
{
	char* p;

	for(p = msg; *p; p++)
		if(*p & 0x80 || *p < 0x20)
			return 0;

	return 1;
}

static void test(char* msg, uint8_t hash[32])
{
	uint8_t temp[32];

	sha256(temp, msg, strlen(msg));

	if(!memcmp(hash, temp, 20))
		return;

	if(!printable(msg))
		msg = "<non-printable>";

	printf("FAIL %s\n", msg);

	dump("exp", hash);
	dump("got", temp);

	_exit(0xFF);
}

struct test {
	char* input;
	uint8_t hash[32];
} tests[] = {
	/* Empty input */
	{ "", {
		0XE3,0XB0,0XC4,0X42,0X98,0XFC,0X1C,0X14,
		0X9A,0XFB,0XF4,0XC8,0X99,0X6F,0XB9,0X24,
		0X27,0XAE,0X41,0XE4,0X64,0X9B,0X93,0X4C,
		0XA4,0X95,0X99,0X1B,0X78,0X52,0XB8,0X55 } },
	/* RFC TEST1 */
	{ "abc", {
		0xBA,0x78,0x16,0xBF,0x8F,0x01,0xCF,0xEA,
		0x41,0x41,0x40,0xDE,0x5D,0xAE,0x22,0x23,
		0xB0,0x03,0x61,0xA3,0x96,0x17,0x7A,0x9C,
		0xB4,0x10,0xFF,0x61,0xF2,0x00,0x15,0xAD } },
	/* RFC TEST2_1 */
	{ "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", {
		0x24,0x8D,0x6A,0x61,0xD2,0x06,0x38,0xB8,
		0xE5,0xC0,0x26,0x93,0x0C,0x3E,0x60,0x39,
		0xA3,0x3C,0xE4,0x59,0x64,0xFF,0x21,0x67,
		0xF6,0xEC,0xED,0xD4,0x19,0xDB,0x06,0xC1 } },
	{ NULL, { 0 } }
};

int main(void)
{
	struct test* tp;

	for(tp = tests; tp->input; tp++)
		test(tp->input, tp->hash);

	return 0;
}
