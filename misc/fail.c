#include <fail.h>
#include <null.h>

ERRTAG = "fail";
ERRLIST = { RESTASNUMBERS };

int main(void)
{
	fail("blah", NULL, 123);
}
