char* strecbrk(char* p, char* e, char k)
{
	while(p < e)
		if(*p == k)
			break;
		else
			p++;
	return p;
}
