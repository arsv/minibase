#include <string.h>
#include <format.h>
#include <main.h>

char* fmterr(char* p, char* e, int err)
{
	void* ptr = (char*)errlist;

	while(1) {
		byte* lead = ptr;;
		byte code = *lead;
		char* msg = ptr + 1;
		char* end = strpend(msg);

		ptr = end + 1;

		if(!code || !end)
			break;
		if(code != -err)
			continue;

		return fmtstre(p, e, msg, end);
	}

	return fmtint(p, e, err);
};
