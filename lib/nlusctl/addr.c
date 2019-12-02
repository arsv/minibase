#include <bits/socket/unix.h>
#include <nlusctl.h>

int uc_address(struct sockaddr_un* addr, const char* path)
{
	char* p = addr->path;
	char* e = p + sizeof(addr->path);
	const char* q = path;
	char c;

	addr->family = AF_UNIX;

	while(p < e) {
		if((c = *q++))
			*p++ = c;
		else break;
	} if(p >= e) {
		return -ENAMETOOLONG;
	}

	*p = '\0';

	return 0;
}
