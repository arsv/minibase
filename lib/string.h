#include <bits/types.h>
#include <null.h>

void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* s, int c, size_t n);

char* strcbrk(char* str, char c);
int strcmp(const char* a, const char* b);
char* strecbrk(char* p, char* e, char k);
unsigned long strlen(const char* a);
int strncmp(const char* a, const char* b, unsigned long n);
char* strqbrk(char* s, const char *accept);
char* strstr(const char* str, const char* sub);
