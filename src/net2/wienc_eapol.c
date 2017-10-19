#include <bits/socket/packet.h>
#include <bits/ioctl/socket.h>
#include <bits/arp.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <string.h>
#include <endian.h>
#include <printf.h>
#include <util.h>

#include "wienc.h"
#include "wienc_crypto.h"
#include "wienc_eapol.h"

char* ifname;
int ifindex;
int rawsock;

int eapolstate;
int eapolsends;

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

static void send_packet_2(void);
static void send_packet_4(void);
static void send_group_2(void);

/* One-time initialization of the raw socket */

static void open_rawsock(void)
{
	int type = htons(ETH_P_PAE);
	int flags = SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC;
	int fd, ret;

	if((fd = sys_socket(AF_PACKET, flags, type)) < 0)
		quit("socket", "AF_PACKET", fd);

	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = ifindex,
		.protocol = type
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		quit("bind", "AF_PACKET", ret);

	rawsock = fd;
}

void setup_iface(char* name)
{
	int fd = netlink;
	uint nlen = strlen(name);
	struct ifreq ifr;
	long ret;

	if(nlen > sizeof(ifr.name))
		fail("name too long:", name, 0);

	memzero(&ifr, sizeof(ifr));
	memcpy(ifr.name, name, nlen);

	if((ret = sys_ioctl(fd, SIOCGIFINDEX, &ifr)) < 0)
		fail("ioctl SIOCGIFINDEX", name, ret);

	ifname = name;
	ifindex = ifr.ival;

	if((ret = sys_ioctl(fd, SIOCGIFHWADDR, &ifr)) < 0)
		fail("ioctl SIOCGIFHWADDR", name, ret);

	if(ifr.addr.family != ARPHRD_ETHER)
		fail("unexpected hwaddr family on", name, 0);

	memcpy(smac, ifr.addr.data, 6);

	open_rawsock();
}

void reopen_rawsock(void)
{
	if(rawsock >= 0)
		return;

	open_rawsock();
}

/* The rest of the code deals with AP connection */

static void ignore(char* why)
{
	warn("EAPOL", why, 0);
}

static void abort(char* why)
{
	warn("EAPOL", why, 0);
	abort_connection();
}

static void pmk_to_ptk()
{
	uint8_t *mac1, *mac2;
	uint8_t *nonce1, *nonce2;

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

static const char kde_type_gtk[4] = { 0x00, 0x0F, 0xAC, 0x01 };

static void store_gtk(uint8_t* buf)
{
	memcpy(GTK, buf, 16);

	if(!ap.tkipgroup) return;

	memcpy(GTK + 16, buf + 24, 8);
	memcpy(GTK + 24, buf + 16, 8);
}

static int fetch_gtk(char* buf, int len)
{
	struct kde* kd;
	int kdlen, idx;
	int keylen = ap.tkipgroup ? 32 : 16;

	char* ptr = buf;
	char* end = buf + len;

	while(ptr + sizeof(*kd) < end) {
		kd = (struct kde*) ptr;
		kdlen = 2 + kd->len; /* 2 = sizeof(type) + sizeof(len) */
		ptr += kdlen;

		if(ptr > end)
			break;

		if(kd->magic != 0xDD)
			continue;
		if(kd->len != 6 + keylen) /* kd->type[4], flags[1], _[1], GTK[] */
			continue;
		if(memcmp(kd->type, kde_type_gtk, 4))
			continue;
		if(!(idx = kd->data[0] & 0x3))
			return -1;

		gtkindex = idx;
		store_gtk(kd->data + 2);

		return 0;
	}

	return -1;
}

static void fill_rand(void)
{
	int rlen = sizeof(snonce);
	char rand[rlen];

	long fd, rd;
	char* urandom = "/dev/urandom";

	if((fd = sys_open(urandom, O_RDONLY)) < 0)
		quit("open", urandom, fd);
	if((rd = sys_read(fd, rand, rlen)) < 0)
		quit("read", urandom, rd);
	if(rd < rlen)
		quit("read", urandom, 0);

	sys_close(fd);

	memcpy(snonce, rand, sizeof(snonce));
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
}

/* The tricky part here. EAPOL packet 1/4 (which the AP sends in reponse
   to our ASSOCIATE command) may arrive before we see the ASSOCIATE note
   on netlink. These two events come from different fds, and the netlink
   one clearly takes a longer path somewhere in the kernel, so it's not
   exactly unexpected.

   For some additional reasons, we cannot *send* packets over rawsock until
   we're fully associated (they get silently dropped) but somehow we *can*
   receive packets way before that. So by the time we get packet 1/4, we may
   not yet be able to reply. If we try to reply, the packet gets lost and we
   see a retry of 1/4 a bit later instead of the expected 3/4 because the AP
   never gets our 2/4. If we ignore this first 1/4, we'll have to wait for
   the re-send, which takes some time and overall isn't nice.
   
   So instead we prime the EAPOL state machine before we send ASSOCIATE and
   let it receive packet 1/4 early, but only reply with 2/4 once netlink
   reports association. This ensure very fast connection with no packets
   lost/ignored in most cases, and with no need to re-send anything.

   Now since it all depends on relative timing of unrelated events, we can
   not be sure it always happens like this. We may get 1/4 after ASSOCIATED
   state change, so we must be ready to reply immediately too. */

void prime_eapol_state(void)
{
	eapolstate = ES_WAITING_1_4;
	eapolsends = 0;
}

void allow_eapol_sends(void)
{
	if(eapolstate == ES_WAITING_1_4)
		eapolsends = 1;
	else
		send_packet_2();
}

static int send_packet(char* buf, int len)
{
	int fd = rawsock;
	struct sockaddr_ll dest;
	long wr;

	memzero(&dest, sizeof(dest));
	dest.family = AF_PACKET;
	dest.protocol = htons(ETH_P_PAE);
	dest.ifindex = ifindex;
	memcpy(dest.addr, amac, 6);

	if((wr = sys_sendto(fd, buf, len, 0, &dest, sizeof(dest))) < 0)
		warn("send", NULL, wr);
	else if(wr != len)
		warn("send", "incomplete", 0);
	else
		return 0;

	abort_connection();

	return -1;
}

static int checkbits(struct eapolkey* ek, int bits)
{
	int keyinfo = ntohs(ek->keyinfo);
	int keytype = keyinfo & KI_TYPEMASK;
	int mask = KI_PAIRWISE | KI_ACK | KI_SECURE | KI_MIC | KI_ENCRYPTED;

	if(keytype != KI_SHA)
		return -1;
	if((keyinfo & mask) != bits)
		return -1;

	return 0;
}

static void recv_packet_1(struct eapolkey* ek)
{
	/* wpa_supplicant does not check ek->version */

	if(ek->type != EAPOL_KEY_RSN)
		return abort("packet 1/4 wrong type");
	if(checkbits(ek, KI_PAIRWISE | KI_ACK))
		return ignore("packet 1/4 wrong bits");

	memcpy(anonce, ek->nonce, sizeof(anonce));
	memcpy(replay, ek->replay, sizeof(replay));

	fill_rand();
	pmk_to_ptk();

	if(eapolsends)
		return send_packet_2();
	else
		eapolstate = ES_WAITING_2_4;
}

static void send_packet_2(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;

	ek->version = 1;
	ek->pactype = EAPOL_KEY;
	ek->type = EAPOL_KEY_RSN;
	ek->keyinfo = htons(KI_SHA | KI_PAIRWISE | KI_MIC);
	ek->keylen = 16;
	memcpy(ek->replay, replay, sizeof(replay));
	memcpy(ek->nonce, snonce, sizeof(snonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	const char* payload = ap.ies;
	int paylen = ap.iesize;
	int paclen = sizeof(*ek) + paylen;

	ek->paylen = htons(paylen);
	ek->paclen = htons(paclen - 4);
	memcpy(ek->payload, payload, paylen);

	make_mic(ek->mic, KCK, packet, paclen);

	if(send_packet(packet, paclen))
		return;

	eapolstate = ES_WAITING_3_4;
}

static void recv_packet_3(struct eapolkey* ek)
{
	char* pacbuf = (char*)ek;
	int paclen = 4 + ntohs(ek->paclen);
	int bits = KI_PAIRWISE | KI_ACK | KI_MIC | KI_ENCRYPTED | KI_SECURE;

	if(checkbits(ek, bits))
		return ignore("packet 3/4 wrong bits");

	if(memcmp(anonce, ek->nonce, sizeof(anonce)))
		return abort("packet 3/4 nonce changed");
	if(memcmp(replay, ek->replay, sizeof(replay)) >= 0)
		return abort("packet 3/4 replay fail");
	if(check_mic(ek->mic, KCK, pacbuf, paclen))
		return abort("packet 3/4 bad MIC");

	char* payload = ek->payload;
	int paylen = ntohs(ek->paylen);

	if(unwrap_key(KEK, payload, paylen))
		return abort("packet 3/4 cannot unwrap");
	if(fetch_gtk(payload + 8, paylen - 8))
		return abort("packet 3/4 cannot fetch GTK");

	memcpy(RSC, ek->rsc, 6); /* it's 8 bytes but only 6 are used */
	memcpy(replay, ek->replay, sizeof(replay));

	return send_packet_4();
}

static void send_packet_4(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;

	ek->version = 1;
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

	if(send_packet(packet, paclen))
		return;

	eapolstate = ES_NEGOTIATED;

	upload_ptk();
	upload_gtk();
	cleanup_keys();

	handle_connect();
}

static void recv_group_1(struct eapolkey* ek)
{
	char* pacbuf = (char*)ek;
	int paclen = 4 + ntohs(ek->paclen);

	if(ek->type != EAPOL_KEY_RSN)
		return ignore("re-keying with a different key type");
	if(checkbits(ek, KI_SECURE | KI_ENCRYPTED | KI_ACK | KI_MIC))
		return ignore("not a rekey request packet");
	if(memcmp(replay, ek->replay, sizeof(replay)) >= 0)
		return ignore("packet 1/2 replay");
	if(check_mic(ek->mic, KCK, pacbuf, paclen))
		return ignore("packet 1/2 bad MIC");

	char* payload = ek->payload;
	int paylen = ntohs(ek->paylen);

	if(unwrap_key(KEK, payload, paylen))
		return abort("packet 1/2 cannot unwrap");
	if(fetch_gtk(payload + 8, paylen - 8))
		return abort("packet 1/2 cannot fetch GTK");

	memcpy(RSC, ek->rsc, 6); /* it's 8 bytes but only 6 are used */
	memcpy(replay, ek->replay, sizeof(replay));

	return send_group_2();
}

void send_group_2(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;

	ek->version = 1;
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

	if(send_packet(packet, paclen))
		return;

	warn("group re-keying completed", NULL, 0);
	upload_gtk();
}

static void dispatch(struct eapolkey* ek)
{
	switch(eapolstate) {
		case ES_WAITING_1_4: return recv_packet_1(ek);
		case ES_WAITING_2_4: return recv_packet_1(ek);
		case ES_WAITING_3_4: return recv_packet_3(ek);
		case ES_NEGOTIATED: return recv_group_1(ek);
		default: return ignore("unexpected packet");
	}
}

void handle_rawsock(void)
{
	struct sockaddr_ll sender;
	int psize = sizeof(packet);
	int asize = sizeof(sender);
	int fd = rawsock;
	int rd;

	if((rd = sys_recvfrom(fd, packet, psize, 0, &sender, &asize)) < 0)
		return warn("EAPOL", NULL, rd);

	if(memcmp(ap.bssid, sender.addr, 6))
		return warn("EAPOL", "stray packet", 0);

	struct eapolkey* ek = (struct eapolkey*) packet;
	int eksize = sizeof(*ek);

	if(rd < eksize)
		return ignore("packet too short");
	if(ntohs(ek->paclen) + 4 != rd)
		return ignore("packet size mismatch");
	if(eksize + ntohs(ek->paylen) > rd)
		return ignore("truncated payload");
	if(ek->pactype != EAPOL_KEY)
		return ignore("not a KEY packet");

	return dispatch(ek);
}
