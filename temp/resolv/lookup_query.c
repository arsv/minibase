#include <bits/socket/inet.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/ppoll.h>

#include <format.h>
#include <string.h>
#include <endian.h>
#include <util.h>

#include "dns.h"
#include "lookup.h"

static void fail_ns_error(char* name, int rc) noreturn;

static void fail_ns_error(char* name, int rc)
{
	char* msg;

	switch(rc) {
		case DNSF_RC_SERVER: msg = "server error looking for"; break;
		case DNSF_RC_FORMAT: msg = "format error looking for"; break;
		case DNSF_RC_NAME:   msg = "not found:"; break;
		case DNSF_RC_NOTIMPL: msg = "not implemented:"; break;
		case DNSF_RC_REFUSED: msg = "query refused for"; break;
		case DNSF_RC_NOTAUTH: msg = "not authorative for"; break;
		case DNSF_RC_NOTZONE: msg = "not in zone:"; break;
		default: fail("ns error", NULL, rc);
	}

	fail(msg, name, 0);
}

static void prep_socket(CTX)
{
	int fd, ret;

	struct sockaddr_in self = {
		.family = AF_INET,
		.port = 0,
		.ip = { 0, 0, 0, 0 }
	};

	if((fd = sys_socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = sys_bind(fd, &self, sizeof(self))) < 0)
		fail("bind", NULL, ret);

	ctx->fd = fd;
}

static void free_socket(CTX)
{
	sys_close(ctx->fd);
	ctx->fd = -1;
}

static void prep_ident(CTX)
{
	int fd, rd;
	char* name = "/dev/urandom";

	if(ctx->avail >= 2)
		goto got;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((rd = sys_read(fd, ctx->rand, sizeof(ctx->rand))) < 0)
		fail("read", name, rd);
	if(rd < 2)
		fail("cannot get enough random data", NULL, 0);

	sys_close(fd);
	ctx->avail = rd;
got:
	ctx->avail -= 2;
	memcpy(ctx->id, ctx->rand + ctx->avail, 2);
	memcpy(ctx->data, ctx->id, 2);
}

static void warnip(const char* msg, byte ip[4], int ret)
{
	FMTBUF(p, e, ipstr, 20);
	p = fmtip(p, e, ip);
	FMTEND(p, e);

	warn(msg, ipstr, ret);
}

static int send_packet(CTX, void* buf, int len, byte ip[4])
{
	int ret, fd = ctx->fd;
	struct sockaddr_in to = {
		.family = AF_INET,
		.port = htons(53),
	};

	memcpy(to.ip, ip, 4);

	if((ret = sys_sendto(fd, buf, len, 0, &to, sizeof(to))) < 0)
		warnip("send", ip, ret);

	return ret;
}

static int recv_packet(CTX, byte ip[4])
{
	int ret, fd = ctx->fd;
	struct sockaddr_in from;
	int fromlen = sizeof(from);

	byte* buf = ctx->data;
	int max = ctx->size;

	if((ret = sys_recvfrom(fd, buf, max, 0, &from, &fromlen)) < 0)
		return ret;
	if(memcmp(from.ip, ip, 4))
		return 0;

	return ret;
}

static int valid_packet(CTX)
{
	if(ctx->len < sizeof(struct dnshdr))
		return 0;
	if(memcmp(ctx->data, ctx->id, 2))
		return 0;

	return 1;
}

static int wait_readable(CTX, struct timespec* ts, byte ip[4])
{
	struct pollfd pfd = { .fd = ctx->fd, .events = POLLIN };

	int ret = sys_ppoll(&pfd, 1, ts, NULL);

	if(ret == 0)
		warnip(NULL, ip, (ret = -ETIMEDOUT));
	else if(ret < 0)
		fail("ppoll", NULL, ret);

	return ret;
}

static struct dnshdr* send_recv(CTX, byte* buf, int len, byte ip[4])
{
	int ret;
	struct timespec ts = { 2, 0 };

	prep_socket(ctx);
	prep_ident(ctx);

	if((ret = send_packet(ctx, buf, len, ip)) < 0)
		goto out;

	while(1) {
		if((ret = wait_readable(ctx, &ts, ip)) < 0)
			goto out;
		if((ret = recv_packet(ctx, ip)) != 0)
			goto out;
		if(ret && valid_packet(ctx))
			goto out;
	}
out:
	free_socket(ctx);

	if(ret < (int)sizeof(struct dnshdr))
		return NULL;

	ctx->len = ret;
	ctx->ptr = sizeof(struct dnshdr);

	return (struct dnshdr*)(ctx->data);
}

static byte* put_short(byte* ptr, int val)
{
	*ptr++ = ((val >> 8) & 0xFF);
	*ptr++ = ((val >> 0) & 0xFF);
	return ptr;
}

static void prep_request(CTX, byte* buf, int* size, char* name, ushort type)
{
	int nlen = strlen(name);
	struct dnshdr* dh = (struct dnshdr*) buf;
	byte* ptr = buf + sizeof(*dh);
	byte* end = buf + *size - 5;

	memzero(dh, sizeof(*dh));

	dh->flags = htons(DNSF_RD);
	dh->qdcount = htons(1);

	char* p = name;
	char* e = name + nlen;

	while(p < e) {
		char* q = strecbrk(p, e, '.');
		long partlen = q - p;

		if(partlen > 63)
			fail("name component too long", NULL, 0);
		if(partlen > end - ptr - 1)
			fail("name too long", NULL, partlen);

		*ptr = (byte)partlen;
		memcpy(ptr + 1, p, partlen);
		ptr += 1 + partlen;

		p = q + 1;
	}

	*ptr++ = 0x00;
	ptr = put_short(ptr, type);
	ptr = put_short(ptr, DNS_CLASS_IN);

	*size = ptr - buf;
}

struct dnshdr* run_request(CTX, char* name, ushort type)
{
	struct dnshdr* dh;
	int namelen = strlen(name);
	int hdrlen = sizeof(*dh);
	int txlen = hdrlen+namelen+10;
	byte txbuf[txlen];
	int count = ctx->nscount;

	int rc = DNSF_RC_SUCCESS;

	prep_request(ctx, txbuf, &txlen, name, type);

	for(int i = 0; i < count; i++) {
		byte* ip = ctx->nsaddr[i];

		if(!(dh = send_recv(ctx, txbuf, txlen, ip)))
			continue;

		rc = ntohs(dh->flags) & DNSF_RC;

		if(rc == DNSF_RC_SERVER)
			continue;
		if(rc != DNSF_RC_SUCCESS)
			fail_ns_error(name, rc);

		if(i > 0) {
			memcpy(&ctx->nsaddr[0], ip, 4);
			memzero(ip, 4);
			ctx->nscount = 1;
		}

		return dh;
	}

	if(rc != DNSF_RC_SUCCESS) /* report the last error encountered */
		fail_ns_error(name, rc);
	else
		fail("no usable nameservers", NULL, 0);
}
