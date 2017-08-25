#include <errnames.h>

extern const char errtag[];
extern const char errlist[];

#define ERRTAG(s) \
	const char errtag[] = s;
#define ERRLIST(s) \
	const char errlist[] = s "\0";
