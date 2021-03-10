#include <string.h>
#include <util.h>

char* getenv(char** envp, char* key)
{
	char *p, **pp;
	int max = 1024;
	int len = strnlen(key, max);

	if(len >= max)
		goto out;

	for(pp = envp; (p = *pp); pp++)
		if(!strncmp(p, key, len) && p[len] == '=')
			return p + len + 1;
out:
	return NULL;
}
