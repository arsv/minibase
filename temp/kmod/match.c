#include <string.h>
#include <printf.h>

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* skip_space(char* p, char* e)
{
	while(p < e && isspace(*p))
		p++;
	return p;
}

static char* word(char* p, char* e, char* word)
{
	int len = strlen(word);

	if(e - p < len)
		return NULL;
	if(strncmp(p, word, len))
		return NULL;
	if(p + len >= e)
		return e;
	if(!isspace(p[len]))
		return NULL;

	return skip_space(p + len, e);
}


static char* match_alias(char* ls, char* le, char* name, int nlen)
{
	(void)nlen;
	char* p = ls;
	char* e = le;
	char* n = name;

	if(!(p = word(p, e, "alias")))
		return NULL;

	while(*n && p < e) {
		if(isspace(*p)) {
			break;
		} else if(*p == '*') {
			p++;
			while(*n && *n != *p)
				n++;
		} else if(*p != *n) {
			return NULL;
		} else {
			p++;
			n++;
		};
	}

	if(p < e && *p == '*')
		p++; /* skip trailing * matching nothing */
	if(*n || p >= e || !isspace(*p))
		return NULL;

	return skip_space(p, e);
}

int main(void)
{
	char* name = "pci:v0000104Cd00008241sv00001028sd00000510bc0Csc03i30";
	int nlen = strlen(name);

	char line[] = "alias pci:v*d*sv*sd*bc0Csc03i30* xhci_pci\n";
	char* lend = line + strlen(line)-1;

	char* out = match_alias(line, lend, name, nlen);

	printf("%s\n", out);

	return 0;
}
