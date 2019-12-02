#include <sys/socket.h>
#include <nlusctl.h>

struct ucattr* uc_msg(void* buf, int len)
{
	struct ucattr* msg;
	int full = 2 + len;

	if(full < sizeof(*msg))
		return NULL;

	msg = buf;
	msg->len = full;

	return msg;
}
