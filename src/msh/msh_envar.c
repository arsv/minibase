#include <format.h>
#include <util.h>

#include "msh.h"

char* match(char* envline, char* var)
{
	char* a = envline;
	char* b = var;

	while(*a == *b) {
		a++;
		b++;
	} if(*b || *a != '=')
		return NULL;

	return a + 1;
}

char* valueof(struct sh* ctx, char* var)
{
	char** e;
	char* v;

	for(e = ctx->envp; *e; e++)
		if((v = match(*e, var)))
			return v;

	return "";
}

