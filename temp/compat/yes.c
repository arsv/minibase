#include <sys/file.h>
#include <string.h>

/* We do one syscall per line here, instead of making a huge
   buffer and writing it all at once. This is because
   the output will likely be read one line at a time.

   Also this should give the user (marginally) more chances
   to stop the output.

   Code-wise this is echo with all the parsing removed,
   and the loop added. */

int main(int argc, char** argv)
{
	char* msg;
	int len;

	if(argc > 2) {
		const char* err = "too many arguments\n";
		sys_write(STDERR, err, strlen(err));
		return -1;
	}

	if(argc > 1) {
		len = strlen(argv[1]);
		msg = alloca(len + 2);
		memcpy(msg, argv[1], len);
		msg[len++] = '\n';
		msg[len] = '\0';
	} else {
		msg = "y\n";
		len = 2;
	}

	while(sys_write(1, msg, len) > 0)
		;

	return -1;
}
