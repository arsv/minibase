#include <bits/ints.h>

void pbkdf2_sha1(uint8_t* psk, int len,
                 char* pass, int passlen,
                 char* salt, int saltlen, int iters);
