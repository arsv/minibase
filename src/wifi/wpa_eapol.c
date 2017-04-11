#include <bits/packet.h>
#include <bits/ioctl/socket.h>
#include <bits/arp.h>

#include <sys/socket.h>
#include <sys/recv.h>
#include <sys/recvfrom.h>
#include <sys/sendto.h>
#include <sys/bind.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/close.h>
#include <sys/ioctl.h>
#include <sys/pause.h>

#include <string.h>
#include <endian.h>
#include <fail.h>

#include "wpa.h"
#include "wpa_crypt.h"
#include "wpa_eapol.h"

/* Once the radio level connection has been established by the NL code,
   there's a usable ethernet-style link to the AP. There's no encryption
   yet however, and the AP does not let any packets through except for
   the EHT_P_PAE (type 0x888E) key negotiation packets.

   The keys are negotiated by sending packets over this ethernet link.
   It's a 4-way handshake, 2 packets in and 2 packets out, and it is
   initiated by the AP. Well actually not, it's initiated by the client
   (that's us) sending the right IEs with the ASSOCIATE command back
   in NL code, but here at EAPOL level it looks like the AP talks first.

   The final result of negotiations is PTK and GTK */

int rawsock;
char packet[1024];

uint8_t amac[6]; /* A = authenticator (AP) */
uint8_t smac[6]; /* S = supplicant (client) */

uint8_t anonce[32];
uint8_t snonce[32];

uint8_t replay[8];

uint8_t PSK[32];

uint8_t KCK[16]; /* key check key, for computing MICs */
uint8_t KEK[16]; /* key encryption key, for AES unwrapping */
uint8_t PTK[16]; /* pairwise key (just TK in 802.11 terms) */
uint8_t GTK[32]; /* group temporary key */
uint8_t RSC[6];  /* ATTR_KEY_SEQ for GTK */

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

/* Message 3 comes with a bunch of IEs (apparently?), among which
   there should be one with the GTK. The whole thing is really
   messed up, so refer to the standard for clues. KDE structure
   comes directly from 802.11, it's actually a kind of tagged
   union there but we only need a single case, namely the GTK KDE. */

/* Ref. IEEE 802.11-2012 Table 11-6 */
static const char kde_type_gtk[4] = { 0x00, 0x0F, 0xAC, 0x01 };

/* From wpa_supplicant: swap Tx/Rx for Michael MIC. No idea where
   this comes from, but it's necessary to get the right key.

   Only applies to TKIP. In CCMP mode, the key is 16 bytes and
   there's no need to swap anything.  */

static void store_gtk(uint8_t* buf)
{
	memcpy(GTK, buf, 16);

	if(tkipgroup) {
		memcpy(GTK + 16, buf + 24, 8);
		memcpy(GTK + 24, buf + 16, 8);
	}
}

static void fetch_gtk(char* buf, int len)
{
	struct kde* kd;
	int kdlen;
	int keylen = tkipgroup ? 32 : 16;

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

		/* Flags in kd->data[0] of GTK KDEs;
		   Ref. IEEE 802.11-2012 Fig. 11-31 */
		if((kd->data[0] & 0x3) != 0x01)
			quit("bad GTK index", NULL, 0);

		store_gtk(kd->data + 2);
		return;
	}

	quit("no group key receieved", NULL, 0);
}

static struct eapolkey* recv_eapol(uint8_t mac[6])
{
	struct sockaddr_ll sender;
	int psize = sizeof(packet);
	int asize = sizeof(sender);
	int fd = rawsock;

	long rd = sysrecvfrom(fd, packet, psize, 0, &sender, &asize);

	struct eapolkey* ek = (struct eapolkey*) packet;
	int eksize = sizeof(*ek);

	if(rd < 0)
		fail("recv", "PF_PACKET", rd);
	if(rd < eksize)
		return NULL; /* packet too short */
	if(ntohs(ek->paclen) + 4 != rd)
		return NULL; /* packet size mismatch */
	if(eksize + ntohs(ek->paylen) > rd)
		return NULL; /* truncated payload */
	if(ek->pactype != EAPOL_KEY)
		return 0;

	memcpy(mac, sender.addr, 6);

	return ek;
}

static struct eapolkey* recv_valid(uint8_t mac[6])
{
	struct eapolkey* ek;

	while(!(ek = recv_eapol(mac)))
		;

	return ek;
}

static void send_packet(char* buf, int len)
{
	int fd = rawsock;
	struct sockaddr_ll dest;
	long wr;

	dest.family = AF_PACKET;
	dest.protocol = htons(ETH_P_PAE);
	dest.ifindex = ifindex;
	memcpy(dest.addr, amac, 6);

	if((wr = syssendto(fd, buf, len, 0, &dest, sizeof(dest))) < 0)
		fail("send", "PF_PACKET", wr);
}

/* The standard says (roughly) that we should silently drop any
   unexpected packets. However, if we see something other than
   4-way handshake right after the connection has been established,
   we're doing something really wrong and should probably abort
   right away.
   
   Ignoring the packet will just delay the inevitable, and possibly
   hide what's really going on. I.e. the user will see "timeout"
   when in fact there's something wrong with the AP, or there are
   injected packets in the channel. */

void recv_packet_1(void)
{
	struct eapolkey* ek = recv_valid(amac);
		
	/* wpa_supplicant does not check ek->version */

	if(ek->pactype != EAPOL_KEY)
		fail("packet 1/4 not a key", NULL, 0);
	if(ek->type != EAPOL_KEY_RSN)
		fail("packet 1/4 wrong type", NULL, 0);

	int keyinfo = ntohs(ek->keyinfo);
	int keytype = keyinfo & KI_TYPEMASK;

	if(keytype != KI_SHA)
		fail("packet 1/4 wrong keytype", NULL, 0);
	if(!(keyinfo & KI_PAIRWISE))
		fail("packet 1/4 no PAIRWISE bit", NULL, 0);
	if(!(keyinfo & KI_ACK))
		fail("packet 1/4 no ACK bit", NULL, 0);

	memcpy(anonce, ek->nonce, sizeof(anonce));
	memcpy(replay, ek->replay, sizeof(replay));

	pmk_to_ptk();
}

/* Packet 2 is sent without any payload. This seems to work well.

   wpa_supplicant sends ies[] as the payload for this packet.
   Reasons for that are not clear. The IEs are sent with ASSOCIATE
   command anyway, no point in repeating them, and IEs are not
   KDE-formatted, which is against what the standard says about
   the payload. */

void send_packet_2(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;

	//ek->version = 1; /* reused */
	//ek->pactype = EAPOL_KEY;  /* reused */
	//ek->type = EAPOL_KEY_RSN; /* reused */
	ek->keyinfo = htons(KI_SHA | KI_PAIRWISE | KI_MIC);
	//ek->keylen = 16; /* reused; CCMP key is 16 bytes long */
	//ek->replay is reused
	memcpy(ek->nonce, snonce, sizeof(snonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	int paclen = sizeof(*ek);

	ek->paylen = htons(0); /* IEs could have been here */
	ek->paclen = htons(paclen - 4);

	make_mic(ek->mic, KCK, packet, paclen);

	send_packet(packet, paclen);
}

void recv_packet_3(void)
{
	uint8_t mac[6];
	struct eapolkey* ek = recv_valid(mac);

	char* pacbuf = (char*)ek;
	int paclen = 4 + ntohs(ek->paclen);

	if(memcmp(amac, mac, 6))
		fail("packet 3/4 from another host", NULL, 0);
	if(memcmp(anonce, ek->nonce, sizeof(anonce)))
		fail("packet 3/4 nonce changed", NULL, 0);
	if(memcmp(replay, ek->replay, sizeof(replay)) >= 0)
		fail("packet 3/4 replay fail", NULL, 0);
	if(check_mic(ek->mic, KCK, pacbuf, paclen))
		fail("packet 3/4 bad MIC", NULL, 0);

	int keyinfo = ntohs(ek->keyinfo);

	if(!(keyinfo & KI_INSTALL))
		fail("packet 3/4 no INSTALL bit", NULL, 0);
	if(!(keyinfo & KI_ENCRYPTED))
		fail("packet 3/4 not encrypted", NULL, 0);

	char* payload = ek->payload;
	int paylen = ntohs(ek->paylen);

	if(unwrap_key(KEK, payload, paylen))
		fail("packet 3/4 cannot unwrap GTK", NULL, 0);

	fetch_gtk(payload + 8, paylen - 8);

	memcpy(RSC, ek->rsc, 6); /* it's 8 bytes but only 6 are used */
	memcpy(replay, ek->replay, sizeof(replay));
}

/* Packet 4 is just a confirmation, nothing significant is being
   transmitted here. However, this packet effectively changes
   the state of the link, making the kernel return EINPROGRESS
   on any subsequent authentication/association requests.

   Until this packet goes through, re-connection to a different
   station is possible without explicit deauth requests as long
   as the requests are not issued too fast.

   Not sending this confirmation causes the AP to deauthenticate
   the client after a sub-second timeout. */ 

void send_packet_4(void)
{
	struct eapolkey* ek = (struct eapolkey*) packet;

	//ek->version = 1; /* reused */
	//ek->pactype = 3; /* reused */
	//ek->type = 2; /* reused */
	ek->keyinfo = htons(KI_SHA | KI_PAIRWISE | KI_MIC | KI_SECURE);
	ek->keylen = 0;
	//ek->replay is reused
	memzero(ek->nonce, sizeof(snonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	int paclen = sizeof(*ek);

	ek->paylen = htons(0);
	ek->paclen = htons(paclen);

	make_mic(ek->mic, KCK, packet, paclen);

	send_packet(packet, paclen);
}

static void fill_smac(void)
{
	int fd = rawsock;
	struct ifreq ifr;
	long ret;

	memzero(&ifr, sizeof(ifr));
	memcpy(ifr.name, ifname, IFNAMESIZ);

	if((ret = sysioctl(fd, SIOCGIFHWADDR, &ifr)) < 0)
		fail("ioctl SIOCGIFHWADDR", ifname, ret);

	if(ifr.addr.sa_family != ARPHRD_ETHER)
		fail("unexpected hwaddr family on", ifname, 0);

	memcpy(smac, ifr.addr.sa_data, 6);
}

static void fill_rand(void)
{
	int rlen = sizeof(snonce);
	char rand[rlen];

	long fd, rd;
	char* urandom = "/dev/urandom";

	if((fd = sysopen(urandom, O_RDONLY)) < 0)
		fail("open", urandom, fd);

	if((rd = sysread(fd, rand, rlen)) < 0)
		fail("read", urandom, rd);
	if(rd < rlen)
		fail("read", urandom, 0);

	sysclose(fd);

	memcpy(snonce, rand, sizeof(snonce));
}

/* The socket is bound to the device before ASSOCIATE/CONNECT
   events to avoid possible race conditions with packet 1 arriving
   before we start listening. Negotiations start after CONNECT
   event arrives. */

void open_rawsock()
{
	int ethtype = htons(ETH_P_PAE);
	long ret;

	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = ifindex,
		.protocol = ethtype
	};

	if((ret = syssocket(PF_PACKET, SOCK_DGRAM, ethtype)) < 0)
		fail("socket", "PF_PACKET", ret);

	rawsock = ret;

	if((ret = sysbind(ret, &addr, sizeof(addr))) < 0)
		fail("bind", "AF_PACKET", ret);
}

void negotiate_keys(void)
{
	fill_smac();
	fill_rand();

	recv_packet_1();
	send_packet_2();

	recv_packet_3();
	send_packet_4();
}

void cleanup_keys(void)
{
	memzero(packet, sizeof(packet));
	memzero(anonce, sizeof(anonce));
	memzero(snonce, sizeof(snonce));
	memzero(PTK, sizeof(PTK));
	memzero(GTK, sizeof(GTK));
	/* we may need KCK and KEK for GTK rekeying */
}

/* Re-keying exchange mostly repeats messages 3 and 4, with minor
   changes. But it gets called in ppoll loop, so got to be careful
   here not to block. Return 0 here means no re-keying happened,
   and the caller should not re-upload GTK to the card.

   This exchange happens over an encrypted connection.
   Things like MIC failures should not happen, and if they do
   it's likely a good reason to disconnect.

   UNTESTED! my AP cannot rekey apparently, wtf.

   Ref. IEEE 80211-2012 11.6.7 Group Key Handshake */

void take_group_1(struct eapolkey* ek, uint8_t mac[6])
{
	char* pacbuf = (char*)ek;
	int paclen = 4 + ntohs(ek->paclen);

	if(memcmp(amac, mac, 6))
		quit("group 1/2 from another host", NULL, 0);
	if(memcmp(replay, ek->replay, sizeof(replay)) >= 0)
		quit("group 1/2 replay fail", NULL, 0);
	if(check_mic(ek->mic, KCK, pacbuf, paclen))
		quit("group 1/2 bad MIC", NULL, 0);

	char* payload = ek->payload;
	int paylen = ntohs(ek->paylen);

	if(unwrap_key(KEK, payload, paylen))
		quit("group 1/2 cannot unwrap GTK", NULL, 0);

	fetch_gtk(payload + 8, paylen - 8);

	memcpy(RSC, ek->rsc, 6); /* it's 8 bytes but only 6 are used */
	memcpy(replay, ek->replay, sizeof(replay));
}

void send_group_2(struct eapolkey* ek)
{
	//ek->version = 1; /* reused */
	//ek->pactype = 3; /* reused */
	//ek->type = 2; /* reused */
	ek->keyinfo = htons(KI_MIC | KI_SECURE);
	ek->keylen = 0;
	//ek->replay is reused
	memzero(ek->nonce, sizeof(snonce));
	memzero(ek->iv, sizeof(ek->iv));
	memzero(ek->rsc, sizeof(ek->rsc));
	memzero(ek->mic, sizeof(ek->mic));
	memzero(ek->_reserved, sizeof(ek->_reserved));

	int paclen = sizeof(*ek);

	ek->paylen = htons(0);
	ek->paclen = htons(paclen);

	make_mic(ek->mic, KCK, packet, paclen);

	send_packet(packet, paclen);
}

int group_rekey(void)
{
	uint8_t mac[6];
	struct eapolkey* ek;

	if(!(ek = recv_eapol(mac)))
		return 0;
	if(ek->type != EAPOL_KEY_RSN)
		return 0; /* re-keying w/ a different key type */
	if(ntohs(ek->keyinfo) != (KI_SECURE | KI_ENCRYPTED | KI_ACK))
		return 0; /* not a group rekey packet */

	take_group_1(ek, mac);
	send_group_2(ek);

	return 1;
}
