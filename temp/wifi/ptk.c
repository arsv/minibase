#include <crypto/sha1.h>
#include <format.h>
#include <string.h>

/* PTK derivation playground */

uint8_t anonce[32] = {
	0xee, 0xc7, 0x1d, 0xf2, 0x53, 0x22, 0x9e, 0x0e,
	0x86, 0x0d, 0x2d, 0x31, 0x23, 0xe1, 0x99, 0xde,
	0x17, 0xe1, 0x32, 0xdc, 0xf1, 0x4a, 0xc1, 0xd7,
	0x93, 0x18, 0x6f, 0x04, 0xe6, 0xe5, 0x3a, 0x0a
};

uint8_t snonce[32] = {
	0xb3, 0x7a, 0xeb, 0xad, 0x57, 0x2e, 0x49, 0xac,
	0xa5, 0x17, 0xb0, 0x9b, 0x1b, 0x09, 0x98, 0x74,
	0x95, 0x8a, 0xbe, 0x8d, 0xac, 0x3d, 0x75, 0x1b,
	0x5e, 0x31, 0x74, 0x13, 0x09, 0xe7, 0x25, 0x62
};

uint8_t saddr[6] = { 0xa7, 0x17, 0x0a, 0x3f, 0x45, 0x11 };
uint8_t aaddr[6] = { 0x13, 0x03, 0x74, 0xae, 0x71, 0x98 };

uint8_t psk[32] = {
	0x99, 0x69, 0xb7, 0xe1, 0xd2, 0x91, 0x6a, 0x76,
	0xd7, 0x92, 0x92, 0x8f, 0x02, 0xc0, 0x5b, 0x8f,
	0x22, 0x40, 0x2f, 0x14, 0x7c, 0xa1, 0x95, 0x77,
	0x73, 0x00, 0x4e, 0x7e, 0xf8, 0x77, 0x71, 0xc8
};

union {
	uint8_t buf[60];
	struct {
		uint8_t kck[16];
		uint8_t kek[16];
		uint8_t tk[16];
	};
} ptk;

void dump(char* tag, uint8_t* key, int len)
{
	int i;

	eprintf("%s: ", tag);

	for(i = 0; i < len; i++)
		eprintf("%02X ", key[i]);

	eprintf("\n");
}

void PRF384(uint8_t out[60], uint8_t key[32], char* str,
            uint8_t mac1[6], uint8_t mac2[6],
            uint8_t nonce1[32], uint8_t nonce2[32])
{
	int slen = strlen(str);
	int ilen = slen + 1 + 2*6 + 2*32 + 1; /* exact input len */
	int xlen = ilen + 10; /* guarded buffer len */

	char ibuf[xlen];
	char* p = ibuf;
	char* e = ibuf + sizeof(ibuf);

	p = fmtraw(p, e, str, slen + 1);
	p = fmtraw(p, e, mac1, 6);
	p = fmtraw(p, e, mac2, 6);
	p = fmtraw(p, e, nonce1, 32);
	p = fmtraw(p, e, nonce2, 32);

	int i;

	for(i = 0; i < 3; i++) {
		*p = i;
		hmac_sha1(out + i*20, key, 32, ibuf, ilen);
	}
}

void pmk_to_ptk()
{
	uint8_t *mac1, *mac2;
	uint8_t *nonce1, *nonce2;

	if(memcmp(saddr, aaddr, 6) < 0) {
		mac1 = saddr;
		mac2 = aaddr;
	} else {
		mac1 = aaddr;
		mac2 = saddr;
	}

	if(memcmp(snonce, anonce, 32) < 0) {
		nonce1 = snonce;
		nonce2 = anonce;
	} else {
		nonce1 = anonce;
		nonce2 = snonce;
	}

	dump("Mac1", mac1, 6);
	dump("Mac2", mac2, 6);

	dump("Nnc1", nonce1, 32);
	dump("Nnc2", nonce2, 32);

	dump("PMK", psk, 32);

	PRF384(ptk.buf, psk, "Pairwise key expansion", mac1, mac2, nonce1, nonce2);

	//dump("PTK", ptk.buf, 48);

	dump("KCK", ptk.kck, 16);
	dump("KEK", ptk.kek, 16);
	dump("TK ", ptk.tk, 16);
}

int main(void)
{
	pmk_to_ptk();

}
