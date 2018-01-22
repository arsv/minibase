#include <bits/types.h>

/* Ethernet frame type for EAPOL packets */

#define ETH_P_PAE 0x888E

/* The only pactype value we care about. No idea what's the ref here. */

#define EAPOL_KEY 3

/* eapolkey.type; again, only one acceptable value */

#define EAPOL_KEY_RSN 2

/* Ref. IEEE 802.11-2012 11.6.2 EAPOL-Key frames */

#define KI_TYPEMASK 0x0007
#define KI_MD5    1
#define KI_SHA    2
#define KI_AES    3

#define KI_PAIRWISE   (1<<3)
#define KI_INSTALL    (1<<6)
#define KI_ACK        (1<<7)
#define KI_MIC        (1<<8)
#define KI_SECURE     (1<<9)
#define KI_ERROR     (1<<10)
#define KI_REQUEST   (1<<11)
#define KI_ENCRYPTED (1<<12)
#define KI_SMK       (1<<13)

struct eapolkey {
	uint8_t version;
	uint8_t pactype;
	uint16_t paclen;
	uint8_t type;
	uint16_t keyinfo;
	uint16_t keylen;
	uint8_t replay[8];
	uint8_t nonce[32];
	uint8_t iv[16];
	uint8_t rsc[8];
	uint8_t _reserved[8];
	uint8_t mic[16];
	uint16_t paylen;
	char payload[];
} __attribute__((packed));

/* Ref. IEEE 802.11-2012 Figure 11-30; OUI and data merged into type[]

   This particular structure only described magic=0xDD; magic and len are
   common for all structs apparently but the rest depends on magic, and len
   includes everything starting from type[]. Total packet length is 2 + len. */

struct kde {
	uint8_t magic;
	uint8_t len;
	uint8_t type[4];
	uint8_t data[];
} __attribute__((packed));
