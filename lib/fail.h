#include <null.h>
#include <errtag.h>

void warn(const char* msg, const char* obj, int err);
void fail(const char* msg, const char* obj, int err) __attribute__((noreturn));

/* There's a common routine when we need to fail() in case given
   syscall returns an error. In some cases we may be interested
   in the non-error return as well, as in

	long fd = xchk( sysopen(filename, ...),
			"cannot open", filename);

   Perlish equivalent would be $fd = open() or die;

   The function is so small it does not make sense to link it. */

__attribute__((unused))
static long xchk(long ret, const char* msg, const char* obj)
{
	if(ret < 0)
		fail(msg, obj, ret);
	else
		return ret;
}
