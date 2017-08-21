#include <sys/file.h>

/* Since we have the GNU Hello as a fine example of a properly designed
   GNU package, there's got to be a minibase equivalent.
 
   So this is how we do it in here. */

int main(void)
{
	const char msg[] = "Hello, world!\n";
	int len = sizeof(msg) - 1;

	sys_write(STDOUT, msg, len);

	return 0;
}
