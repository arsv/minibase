#include <string.h>

size_t strelen(char* p, char* e)
{
	if(!p || !e || (p >= e))
		return 0;

	return (e - p);
}
