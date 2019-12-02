#include <bits/types.h>
#include <nlusctl.h>

int uc_repcode(struct ucattr* msg)
{
	return (int16_t)msg->key;
}
