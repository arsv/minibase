#include <sys/write.h>

/* Since we have the GNU Hello as a fine example of properly designed
   GNU package, there's got to be this minibase equivalent.
 
   This is how we do it in here. */

int main(void)
{
	const char msg[] = "Hello, world!\n";
	int len = sizeof(msg) - 1;

	syswrite(STDOUT, msg, len);

	return 0;
}
