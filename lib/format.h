#include <cdefs.h>

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

char* fmtraw(char* p, char* e, void* data, int len);

char* fmterr(char* buf, char* end, int err);

char* fmtchar(char* dst, char* end, char c);
char* fmtbyte(char* dst, char* end, char c);
char* fmtbytes(char* dst, char* end, void* data, uint len);

char* fmti32(char* buf, char* end,  int32_t num);
char* fmtu32(char* buf, char* end, uint32_t num);

char* fmti64(char* buf, char* end,  int64_t num);
char* fmtu64(char* buf, char* end, uint64_t num);

char* fmtint(char* buf, char* end, int num);
char* fmtuint(char* buf, char* end, uint num);
char* fmtlong(char* buf, char* end, long num);
char* fmtulong(char* buf, char* end, ulong num);
char* fmtxlong(char* buf, char* end, long num);
char* fmthex(char* p, char* e, uint n);
char* fmtpad(char* p, char* e, int width, char* q);
char* fmtpad0(char* p, char* e, int width, char* q);
char* fmtpadr(char* p, char* e, int width, char* q);

char* fmtsize(char* p, char* e, uint64_t n);
char* fmtstr(char* dst, char* end, const char* src);
char* fmtstrn(char* dst, char* end, const char* src, int len);
char* fmtstrl(char* dst, char* end, const char* src, int len);

char* fmttm(char* buf, char* end, const struct tm* tm);
char* fmtulp(char* buf, char* end, ulong num, int pad);
char* fmtip(char* p, char* e, uint8_t ip[4]);
char* fmtmac(char* p, char* e, uint8_t mac[6]);

char* parseint(char* buf, int* np);
char* parselong(char* buf, long* np);
char* parseulong(char* buf, ulong* np);
char* parseu64(char* buf, uint64_t* np);
char* parsebyte(char* p, byte* v);
char* parsebytes(char* p, byte* dst, uint len);
char* parseoct(char* buf, int* np);
char* parsehex(char* buf, int* np);

char* parsemac(char* p, byte* mac);
char* parseip(char* p, byte* ip);
char* parseipmask(char* p, byte* ip, byte* mask);
