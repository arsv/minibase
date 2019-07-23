#include <bits/types.h>
#include <bits/socket/packet.h>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/random.h>

#include <crypto/sha1.h>
#include <crypto/aes128.h>

#include <string.h>
#include <endian.h>
#include <format.h>
#include <util.h>

#include "wsupp.h"
#include "wsupp_eapol.h"

/* Once the radio level connection has been established by the NL code,
   there's a usable ethernet-style link to the AP. There's no encryption
   yet however, and the AP does not let any packets through except for
   the EHT_P_PAE (type 0x888E) key negotiation packets.

   The keys are negotiated by sending packets over this ethernet link.
   It's a 4-way handshake, 2 packets in and 2 packets out, and it is
   initiated by the AP. Well actually not, it's initiated by the client
   (that's us) sending the right IEs with the ASSOCIATE command back
   in NL code, but here at EAPOL level it looks like the AP talks first.

   The final result of negotiations is PTK and GTK. */

/* eapolstate */
#define ES_IDLE            0
#define ES_WAITING_1_4     1
#define ES_WAITING_2_4     2
#define ES_WAITING_3_4     3
#define ES_NEGOTIATED      4

/* return codes */
#define IGNORE 0
#define ACCEPTED 0

int rawsock;

int eapolstate;
int eapolsends;

static int version;
byte amac[6]; /* A = authenticator (AP) */
byte smac[6]; /* S = supplicant (client) */

byte anonce[32];
byte snonce[32];

byte replay[8];

byte PSK[32];

byte KCK[16]; /* key check key, for computing MICs */
byte KEK[16]; /* key encryption key, for AES unwrapping */
byte PTK[16]; /* pairwise key (just TK in 802.11 terms) */
byte GTK[32]; /* group temporary key */
byte RSC[6];  /* ATTR_KEY_SEQ for GTK */
int gtkindex;

static char packet[1024];

static int send_packet_2(void);
static int send_packet_4(void);
static int send_group_2(void);

/* A socket bound to an interface enters failed state if the interface
   goes down, which happens during rfkill. If this happens, we have to
   re-open adn re-bind it. Otherwise, there's no problem with the socket
   remaining open across connection, so we do not bother closing it. */

int open_rawsock(void)
{
	int type = htons(ETH_P_PAE);
	int flags = SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	int fd, ret;

	if(rawsock >= 0)
		return 0;

	if((fd = sys_socket(AF_PACKET, flags, type)) < 0) {
		warn("socket", "AF_PACKET", fd);
		return fd;
	}

	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = ifindex,
		.protocol = type
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0) {
		warn("bind", "AF_PACKET", ret);
		sys_close(fd);
		return ret;
	}

	rawsock = fd;
	pollset = 0;

	return 0;
}

void close_rawsock(void)
{
	if(rawsock < 0)
		return;

	sys_close(rawsock);
	rawsock = -1;
	pollset = 0;
}

/* Supplementary crypto routines for EAPOL negotiations.
   Ref. IEEE 802.11-2012 11.6.1.2 PRF

   The standard calls for PRF384 but that's just the same code
   that truncates the result to 48 bytes. In their terms:

       PRF-384(K, A, B) = L(PRF-480(K, A, B), 0, 384)

   To make things a bit easier, K is made 60 bytes (480 bits)
   long and no explicit truncation is preformed. In the caller,
   K is a temporary buffer anyway, the useful stuff gets copied
   out immediately.

   This function also handles concatenation:

       A = str
       B = mac1 | mac2 | nonce1 | nonce2

   HMAC input is then

       A | 0 | B | i

   so there's no point in a dedicated buffer for B. */

static void PRF480(byte out[60], byte key[32], char* str,
        byte mac1[6], byte mac2[6], byte nonce1[32], byte nonce2[32])
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

	for(int i = 0; i < 3; i++) {
		*p = i;
		hmac_sha1(out + i*20, key, 32, ibuf, ilen);
	}
}

/* SHA-1 based message integrity code (MIC) for auth and key management
   scheme (AKM) 00-0F-AC:2, which we requested in association IEs and
   probably checked in packet 1 payload.

   Ref. IEEE 802.11-2012 11.6.3 EAPOL-Key frame construction and processing. */

static void make_mic(byte mic[16], byte kck[16], void* buf, int len)
{
	uint8_t hash[20];
	int kcklen = 16;
	int miclen = 16;

	hmac_sha1(hash, kck, kcklen, buf, len);

	memcpy(mic, hash, miclen);
}

static int check_mic(byte mic[16], byte kck[16], void* buf, int len)
{
	uint8_t hash[20];
	uint8_t copy[16];
	int kcklen = 16;
	int miclen = 16;

	memcpy(copy, mic, miclen);
	memzero(mic, miclen);

	hmac_sha1(hash, kck, kcklen, buf, len);

	int ret = memxcmp(hash, copy, miclen);

	return ret;
}

/* Packet 3 payload (GTK) is wrapped with standard RFC3394 0xA6
   checkblock. We unwrap it in place, and start parsing 8 bytes
   into the data. */

static const byte iv[8] = {
	0xA6, 0xA6, 0xA6, 0xA6,
	0xA6, 0xA6, 0xA6, 0xA6
};

int unwrap_key(uint8_t kek[16], void* buf, int len)
{
	if(len % 8 || len < 16)
		return -1;

	aes128_unwrap(kek, buf, len);

	return memxcmp(buf, iv, 8);
}

static void pmk_to_ptk()
{
	uint8_t *mac1, *mac2;
	uint8_t *nonce1, *nonce2;

	memcpy(smac, ifaddr, 6);
	memcpy(amac, ap.bssid, 6);

	if(memcmp(smac, amac, 6) < 0) {
		mac1 = smac;
		mac2 = amac;
	} else {
		mac1 = amac;
		mac2 = smac;
	}

	if(memcmp(snonce, anonce, 32) < 0) {
		nonce1 = snonce;
		nonce2 = anonce;
	} else {
		nonce1 = anonce;
		nonce2 = snonce;
	}

	uint8_t key[60];

	char* astr = "Pairwise key expansion";
	PRF480(key, PSK, astr, mac1, mac2, nonce1, nonce2);

	memcpy(KCK, key +  0, 16);
	memcpy(KEK, key + 16, 16);
	memcpy(PTK, key + 32, 16);

	memzero(key, sizeof(key));
}

/* The rest of the code deals with AP connection */

/* Message 3 comes with a bunch of IEs (apparently?), among which
   there should be one with the GTK. The whole thing is really
   messed up, so refer to the standard for clues. KDE structure
   comes directly from 802.11, it's a kind of tagged union there
   but we only need a single case, namely the GTK KDE. */

/* Ref. IEEE 802.11-2012 Table 11-6 */
static const char kde_type_gtk[4] = { 0x00, 0x0F, 0xAC, 0x01 };

static int store_gtk(int idx, byte* buf, int len)
{
	int explen = ap.gtklen; /* 16 for CCMP, 32 for TKIP */

	if(len != explen)
		return -1;

	gtkindex = idx;

	memcpy(GTK, buf, 16);

	/* From wpa_supplicant: swap Tx/Rx for Michael MIC for
	   TKIP GTK. No idea where this comes from, but it's
	   necessary to get the right key. */
	if(explen == 32) {
		memcpy(GTK + 16, buf + 24, 8);
		memcpy(GTK + 24, buf + 16, 8);
	}

	return 0;
}

static int fetch_gtk(char* buf, int len)
{
	struct kde* kd;
	int kdlen, idx;

	char* ptr = buf;
	char* end = buf + len;

	while(ptr + sizeof(*kd) < end) {
		kd = (struct kde*) ptr;
		kdlen = 2 + kd->len; /* 2 = sizeof(type) + sizeof(len) */
		ptr += kdlen;

		if(ptr > end)
			break;

		int datalen = kd->len + 2 - sizeof(*kd);

		if(kd->magic != 0xDD)
			continue;
		if(memcmp(kd->type, kde_type_gtk, 4))
			continue;
		if(datalen < 2 + 16) /* flags[1] + pad[1] + min key length */
			continue;
		if(!(idx = kd->data[0] & 0x3)) /* key idx is non-zero for GTK */
			return -1;

		byte* key = kd->data + 2;
		int len = datalen - 2;

		return store_gtk(idx, key, len);
	}

	return -1;
}

static void cleanup_keys(void)
{
	memzero(packet, sizeof(packet));
	memzero(anonce, sizeof(anonce));
	memzero(snonce, sizeof(snonce));
	memzero(PTK, sizeof(PTK));
	memzero(GTK, sizeof(GTK));
	/* we may need KCK and KEK for GTK rekeying */
}

void reset_eapol_state(void)
{
	cleanup_keys();

	memzero(KCK, sizeof(KCK));
	memzero(GTK, sizeof(GTK));
	memzero(KEK, sizeof(KEK));

	memzero(snonce, sizeof(snonce));
	memzero(anonce, sizeof(anonce));
	memzero(amac, sizeof(amac));
	memzero(smac, sizeof(smac));

	version = 0;
}

/* The tricky part here. EAPOL packet 1/4 may arrive before the ASSOCIATE msg
   on netlink, but sending may not work until the link is fully associated.
   Packets sent until then get silently dropped somewhere. So at the time we
   get packet 1/4, we may not yet be able to reply.

   To get around this, we prime the EAPOL state machine before AUTHENTICATE
   request and let it receive packet 1/4 early, but only reply with 2/4 once
   netlink reports association.

   Since it all depends on relative timing of unrelated events, we can not
   be sure it always happens like this. We may get 1/4 after ASSOCIATE msg,
   so we must be ready to reply immediately as well.

   Note on RNG: getrandom() may fail, and if it does it's better to not even
   attempt to start the connection. */

int prime_eapol_state(void)
{
	eapolstate = ES_WAITING_1_4;
	eapolsends = 0;

	return sys_getrandom(snonce, sizeof(snonce), GRND_NONBLOCK);
}

int allow_eapol_sends(void)
{
	if(eapolstate == ES_WAITING_1_4) {
		eapolsends = 1;
		return 0;
	}
	if(eapolstate == ES_WAITING_2_4) {
		int ret = send_packet_2();
		return ret < 0 ? ret : 0;
	}

	/* should never happen */
	return -EINVAL;
}

static int send_packet(char* buf, int len)
{
	int ret, fd = rawsock;
	struct sockaddr_ll dest;

	memzero(&dest, sizeof(dest));
	dest.family = AF_PACKET;
	dest.protocol = htons(ETH_P_PAE);
	dest.ifindex = ifindex;
	dest.halen = 6;
	memcpy(dest.addr, amac, 6);

	if((ret = sys_sendto(fd, buf, len, 0, &dest, sizeof(dest))) < 0)
		warn("send", "EAPOL", ret);
	else if(ret != len)
		ret = -EMSGSIZE;

	return ret;
}

static int ptype(struct eapolkey* ek, int bits)
{
	int keyinfo = ntohs(ek->keyinfo);
	int keytype = keyinfo & KI_TYPEMASK;
	int mask = KI_PAIRWISE | KI_ACK | KI_SECURE | KI_MIC | KI_ENCRYPTED;

	if(keytype != KI_SHA)
		return 0;
	if((keyinfo & mask) != bits)
		return 0;

	return 1;
}

static int recv_packet_1(struct eapolkey* ek)
{
	/* wpa_supplicant does not check ek->version */

	if(ek->type != EAPOL_KEY_RSN)
		return IGNORE; /* wrong type */
	if(!ptype(ek, KI_PAIRWISE | KI_ACK))
		return IGNORE; /* wrong bits */

	version = ek->version;
	memcpy(anonce, ek->nonce, sizeof(anonce));
	memcpy(replay, ek->replay, sizeof(replay));

	pmk_to_ptk();

	if(eapolsends)
		return send_packet_2();

	eapolstate = ES_WAITING_2_4;

	return ACCEPTED;
}

/* Packet 2/4 must carry IEs, the same ones we've sent already
   with the ASSOCIATE command. The point in doing so is not clear,
   but there are APs (like the Android hotspot) that *do* check them
   and bail out if IEs are not there.

   The fact they match exactly the ASSOCIATE payload may be accidental.
   Really needs a reference here. But they do seem to match in practice. */

static int send_packet_2(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;
	int ret;

	ek->version = version;
	ek->pactype = EAPOL_KEY;
	ek->type = EAPOL_KEY_RSN;
	ek->keyinfo = htons(KI_SHA | KI_PAIRWISE | KI_MIC);
	ek->keylen = htons(16);
	memcpy(ek->replay, replay, sizeof(replay));
	memcpy(ek->nonce, snonce, sizeof(snonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	const void* payload = ap.txies;
	int paylen = ap.iesize;
	int paclen = sizeof(*ek) + paylen;

	ek->paylen = htons(paylen);
	ek->paclen = htons(paclen - 4);
	memcpy(ek->payload, payload, paylen);

	make_mic(ek->mic, KCK, packet, paclen);

	if((ret = send_packet(packet, paclen)) < 0)
		return ret;

	eapolstate = ES_WAITING_3_4;

	return 0;
}

static int recv_packet_3(struct eapolkey* ek)
{
	char* pacbuf = (char*)ek;
	int paclen = 4 + ntohs(ek->paclen);

	if(ptype(ek, KI_PAIRWISE | KI_ACK))
		return IGNORE; /* packet 1/4 resend */
	if(!ptype(ek, KI_PAIRWISE | KI_ACK | KI_MIC | KI_ENCRYPTED | KI_SECURE))
		return IGNORE; /* packet 3/4 wrong bits */

	if(memcmp(anonce, ek->nonce, sizeof(anonce)))
		return -EBADMSG; /* nonce changed */
	if(memcmp(replay, ek->replay, sizeof(replay)) >= 0)
		return -EBADMSG; /* replay chech failure */
	if(check_mic(ek->mic, KCK, pacbuf, paclen))
		return -EBADMSG; /* bad MIC */

	char* payload = ek->payload;
	int paylen = ntohs(ek->paylen);

	if(unwrap_key(KEK, payload, paylen))
		return -ENOKEY; /* cannot unwrap */
	if(fetch_gtk(payload + 8, paylen - 8))
		return -ENOKEY; /* cannot fetch GTK */

	memcpy(RSC, ek->rsc, 6); /* it's 8 bytes but only 6 are used */
	memcpy(replay, ek->replay, sizeof(replay));

	return send_packet_4();
}

static int send_packet_4(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;
	int ret;

	ek->version = version;
	ek->pactype = 3;
	ek->type = 2;
	ek->keyinfo = htons(KI_SHA | KI_PAIRWISE | KI_MIC | KI_SECURE);
	ek->keylen = 0;
	memcpy(ek->replay, replay, sizeof(replay));
	memzero(ek->nonce, sizeof(ek->nonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	int paclen = sizeof(*ek);

	ek->paylen = htons(0);
	ek->paclen = htons(paclen - 4);

	make_mic(ek->mic, KCK, packet, paclen);

	if((ret = send_packet(packet, paclen)) < 0)
		return ret;

	eapolstate = ES_NEGOTIATED;

	if((ret = upload_ptk()) < 0)
		return ret;
	if((ret = upload_gtk()) < 0)
		return ret;

	cleanup_keys();
	eapol_success();

	return 0;
}

/* Group rekey packets may arrive at any time, and they are the only
   reason to keep rawsock open past the initial key negotiations.
   The AP decides when to send them, typically once in N hours.

   Because of the way dispatch() below works, any EAPOL packets
   arriving after packet 4/4 has been sent will be treated as
   group rekey request, and rejected if they don't look like one. */

static int recv_group_1(struct eapolkey* ek)
{
	char* pacbuf = (char*)ek;
	int paclen = 4 + ntohs(ek->paclen);

	if(ek->type != EAPOL_KEY_RSN)
		return IGNORE; /* re-keying with a different key type */
	if(!ptype(ek, KI_SECURE | KI_ENCRYPTED | KI_ACK | KI_MIC))
		return IGNORE; /* not a rekey request packet */
	if(memcmp(replay, ek->replay, sizeof(replay)) >= 0)
		return IGNORE; /* replay check fail */
	if(check_mic(ek->mic, KCK, pacbuf, paclen))
		return IGNORE; /* bad MIC */

	char* payload = ek->payload;
	int paylen = ntohs(ek->paylen);

	if(unwrap_key(KEK, payload, paylen))
		return -ENOKEY; /* cannot unwrap */
	if(fetch_gtk(payload + 8, paylen - 8))
		return -ENOKEY; /* cannot fetch GTK */

	memcpy(RSC, ek->rsc, 6); /* it's 8 bytes but only 6 are used */
	memcpy(replay, ek->replay, sizeof(replay));

	return send_group_2();
}

static int send_group_2(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;
	int ret;

	ek->version = version;
	ek->pactype = 3;
	ek->type = 2;
	ek->keyinfo = htons(KI_MIC | KI_SECURE);
	ek->keylen = 0;
	memcpy(ek->replay, replay, sizeof(replay));
	memzero(ek->nonce, sizeof(snonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	int paclen = sizeof(*ek);

	ek->paylen = htons(0);
	ek->paclen = htons(paclen);

	make_mic(ek->mic, KCK, packet, paclen);

	if((ret = send_packet(packet, paclen)) < 0)
		return ret;

	if((ret = upload_gtk()) < 0)
		return ret;

	return 0;
}

static int dispatch(struct eapolkey* ek)
{
	switch(eapolstate) {
		case ES_WAITING_1_4: return recv_packet_1(ek);
		case ES_WAITING_3_4: return recv_packet_3(ek);
		case ES_NEGOTIATED: return recv_group_1(ek);
		default: return IGNORE;
	}
}

void handle_rawsock(void)
{
	struct sockaddr_ll sender;
	int psize = sizeof(packet);
	int asize = sizeof(sender);
	int fd = rawsock;
	int rd, ret;

	if((rd = sys_recvfrom(fd, packet, psize, 0, &sender, &asize)) < 0)
		return warn("EAPOL", NULL, rd);

	if(memcmp(ap.bssid, sender.addr, 6))
		return; /* stray packet */

	struct eapolkey* ek = (struct eapolkey*) packet;
	int eksize = sizeof(*ek);

	if(rd < eksize)
		return; /* packet too short */
	if(ntohs(ek->paclen) + 4 != rd)
		return; /* packet size mismatch */
	if(eksize + ntohs(ek->paylen) > rd)
		return; /* truncated payload */
	if(ek->pactype != EAPOL_KEY)
		return; /* not a KEY packet */

	if((ret = dispatch(ek)) < 0)
		abort_connection(ret);
}
