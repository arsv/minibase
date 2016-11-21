#include <string.h>
#include <util.h>

char* getenv(char** envp, const char* key)
{
	char *p, **pp;
	int len = strlen(key);

	for(pp = envp; (p = *pp); pp++)
		if(!strncmp(p, key, len) && p[len] == '=')
			return p + len + 1;

	return NULL;
}
