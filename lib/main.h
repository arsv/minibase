#include <errnames.h>
#include <cdefs.h>

extern const char errtag[];
extern const char errlist[];

#define ERRTAG(s) \
	const char errtag[] = s;
#define ERRLIST(s) \
	const char errlist[] = s "\0";

/* No envp is passed to main. Normally the compiler would not catch it,
   so make sure there's a prototype for main().

   If needed, envp = argv + argc + 1

   Those few tools that ignore argc/argv should define int main(noargs). */

extern int main(int argc, char** argv);

#define noargs int argc __unused, char** argv __unused
