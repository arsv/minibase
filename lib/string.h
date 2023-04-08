#include <cdefs.h>

void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* av, const void* bv, size_t len);
int memxcmp(const void* av, const void* bv, size_t len);
void memzero(void* a, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int nonzero(void* a, size_t n);

char* strcbrk(char* str, char c);
char* strecbrk(char* p, char* e, char k);
int strcmp(const char* a, const char* b);
int natcmp(const char* a, const char* b);
char* strpend(char* str);
size_t strlen(const char* a);
size_t strnlen(const char* a, size_t max);
size_t strelen(char* str, char* end);
int strcmpn(const char* a, const char* b, size_t n);
int strncmp(const char* a, const char* b, size_t n);
char* strstr(const char* str, const char* sub);
char* strnstr(const char* str, const char* sub, size_t len);
char* strchr(const char* str, int c);
char* strerev(char* p, char* e, char c);
