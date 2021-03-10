#include <format.h>
#include <string.h>

char* fmtstre(char* p, char* e, char* str, char* end)
{
	return fmtraw(p, e, str, strelen(str, end));
}
