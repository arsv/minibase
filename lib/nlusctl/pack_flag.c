#include <nlusctl.h>

void uc_put_flag(struct ucbuf* uc, int key)
{
	(void)uc_put(uc, key, 0, 0);
}
