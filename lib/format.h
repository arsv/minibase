#include <cdefs.h>

extern const char errtag[];

#define FMTUSE(p, e, buf, len) \
	char* p = buf; \
	char* e = buf + len - 1;

#define FMTBUF(p, e, buf, len) \
	char buf[len+1];\
	char* p = buf;\
	char* e = buf + sizeof(buf) - 1;

#define FMTEND(p, e) \
	*p = '\0';
#define FMTENL(p, e) \
	if(p < e) *p++ = '\n';
/* Note FMTENL is used to produce a buffer suitable for
   write()-ing to STDOUT, *not* a 0-terminated string. */

struct tm;

char* fmtraw(char* p, char* e, const void* data, int len);

char* fmterr(char* p, char* e, int err);

char* fmtbyte(char* p, char* e, char c);
char* fmtbytes(char* p, char* e, const void* data, uint len);

char* fmti32(char* p, char* e,  int32_t num);
char* fmtu32(char* p, char* e, uint32_t num);
char* fmtx32(char* p, char* e, uint32_t num);

char* fmti64(char* p, char* e,  int64_t num);
char* fmtu64(char* p, char* e, uint64_t num);
char* fmtx64(char* p, char* e, uint64_t num);

char* fmtint(char* p, char* e, int num);
char* fmtuint(char* p, char* e, uint num);
char* fmtxint(char* p, char* e, uint num);
char* fmtlong(char* p, char* e, long num);
char* fmtulong(char* p, char* e, ulong num);
char* fmtxlong(char* p, char* e, ulong num);
char* fmthex(char* p, char* e, uint n);
char* fmtpad(char* p, char* e, int width, char* q);
char* fmtpad0(char* p, char* e, int width, char* q);
char* fmtpadr(char* p, char* e, int width, char* q);

char* fmtsize(char* p, char* e, uint64_t n);
char* fmtstr(char* p, char* e, const char* src);
char* fmtstrn(char* p, char* e, const char* src, int len);
char* fmtstrl(char* p, char* e, const char* src, int len);

char* fmttm(char* p, char* e, const struct tm* tm);
char* fmtulp(char* p, char* e, ulong num, int pad);
char* fmtip(char* p, char* e, uint8_t ip[4]);
char* fmtmac(char* p, char* e, uint8_t mac[6]);

char* parseint(char* p, int* np);
char* parseuint(char* p, uint* np);
char* parselong(char* p, long* np);
char* parseulong(char* p, ulong* np);
char* parseu64(char* p, uint64_t* np);
char* parsebyte(char* p, byte* v);
char* parsebytes(char* p, byte* buf, uint len);
char* parseoct(char* p, int* np);
char* parsehex(char* p, int* np);
char* parsexlong(char* p, ulong* np);

char* parsemac(char* p, byte* mac);
char* parseip(char* p, byte* ip);
char* parseipmask(char* p, byte* ip, byte* mask);

static inline char* fmtchar(char* p, char* e, char c)
{
	if(p < e)
		*p++ = c;
	return p;
};
