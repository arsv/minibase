static long xchk(long ret, const char* msg, const char* obj)
{
	if(ret < 0)
		fail(msg, obj, -ret);
	else
		return ret;
}
