static inline char* strapc(char* dst, char* end, char c)
{
	if(dst < end)
		*dst++ = c;
	return dst;
}
