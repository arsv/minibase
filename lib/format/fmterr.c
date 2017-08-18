#include <format.h>
#include <errtag.h>

char* fmterr(char* buf, char* end, int err)
{
	const struct errcode* p;

	err = -err;

	for(p = errlist; p->code; p++)
		if(p->code == err)
			break;
	if(p->code)
		return fmtstr(buf, end, p->name);
	else
		return fmti32(buf, end, err);
};
