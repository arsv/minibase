#include <netlink.h>
#include <netlink/pack.h>

#include <string.h>

static void* nc_put_(struct ncbuf* nc, uint key, uint len)
{
	void* buf = nc->brk;
	void* ret = nc->ptr;
	void* end = nc->end;
	uint full = sizeof(struct nlattr) + len;
	uint need = (full + 3) & ~3;
	void* ptr = ret + need;

	if(ret > end || ret < buf) {
		return NULL;
	} if(ptr > end || ptr < buf) {
		nc->ptr = end + 1;
		return NULL;
	} if(ptr < buf + sizeof(struct nlmsg)) {
		return NULL;
	}

	nc->ptr = ptr;

	if(need > full) {
		int* last = ret + (full & ~3);
		*last = 0;
	}

	struct nlattr* at = ret;

	at->type = key;
	at->len = full;

	return at->payload;
}

void nc_put(struct ncbuf* nc, uint key, void* data, uint size)
{
	void* dst = nc_put_(nc, key, size);

	if(dst) memcpy(dst, data, size);
}

void nc_put_int(struct ncbuf* nc, uint key, int val)
{
	int* dst = nc_put_(nc, key, sizeof(*dst));

	if(dst) *dst = val;
}

void nc_put_byte(struct ncbuf* nc, uint key, byte val)
{
	byte* dst = nc_put_(nc, key, sizeof(*dst));

	if(dst) *dst = val;
}

void nc_put_str(struct ncbuf* nc, uint key, char* str)
{
	int len = strlen(str) + 1;
	char* dst = nc_put_(nc, key, len);

	if(dst) memcpy(dst, str, len);
}

void nc_put_flag(struct ncbuf* nc, uint key)
{
	(void)nc_put_(nc, key, 0);
}
