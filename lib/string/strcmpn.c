#include <string.h>

/* Compare a padded string (char[n] a) against a nul-terminated one (char* b).

     char name[3] = { 'a', 'b', 'c' }

     strcmpn(name, "abc", 3) -- no overruns, returns zero
     strcmpn(name, "abcdef", 3) -- returns non-zero, "abc" is not "abcdef"

   Note this is different from strncmp, which effectively compares two padded
   strings of the same length (char[n] a, char[n] b).

     strncmp(name, "abcdef", 3) -- zero, "abc" equals "abc"

   In most cases within this project strcmpn should be used instead of strncmp.

   To avoid confusion (incl. on my own part) it gets a different name,
   and strncmp retains its standard semantics. */

int strcmpn(const char* a, const char* b, size_t n)
{
	const char* e = a + n;

	while(a < e) {
		char ac = *a++;
		char bc = *b++;

		int d = ac - bc;

		if(d || !ac) return d;
	}

	/* The first n characters are equal */

	return -(*b);
}
