#include <bits/types.h>

struct tm;

char* fmtraw(char* p, char* e, void* data, int len);

char* fmtbyte(char* dst, char* end, char c);
char* fmtchar(char* dst, char* end, char c);

char* fmti32(char* buf, char* end,  int32_t num);
char* fmtu32(char* buf, char* end, uint32_t num);

char* fmti64(char* buf, char* end,  int64_t num);
char* fmtu64(char* buf, char* end, uint64_t num);

char* fmtint(char* buf, char* end, int num);
char* fmtuint(char* buf, char* end, unsigned num);
char* fmtlong(char* buf, char* end, long num);
char* fmtulong(char* buf, char* end, unsigned long num);
char* fmtxlong(char* buf, char* end, long num);
char* fmtpad(char* p, char* e, int width, char* q);
char* fmtpad0(char* p, char* e, int width, char* q);

char* fmtsize(char* p, char* e, uint64_t n);
char* fmtstr(char* dst, char* end, const char* src);
char* fmtstrn(char* dst, char* end, const char* src, int len);
char* fmtstrl(char* dst, char* end, const char* src, int len);

char* fmttm(char* buf, char* end, struct tm* tm);
char* fmtulp(char* buf, char* end, unsigned long num, int pad);
char* fmtip(char* p, char* e, uint8_t ip[4]);
char* fmtmac(char* p, char* e, uint8_t mac[6]);

char* parseint(char* buf, int* np);
char* parselong(char* buf, long* np);
char* parseulong(char* buf, unsigned long* np);
char* parsebyte(char* p, uint8_t* v);
char* parsebytes(char* p, uint8_t* dst, int len);

char* parsemac(char* p, uint8_t* mac);
char* parseip(char* p, uint8_t* ip);
char* parseipmask(char* p, uint8_t* ip, uint8_t* mask);

int printf(const char* fmt, ...);
int eprintf(const char* fmt, ...);
