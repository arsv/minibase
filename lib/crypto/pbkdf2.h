#include <bits/types.h>

void pbkdf2_sha1(void* psk, int len,
               void* pass, int passlen,
               void* salt, int saltlen, int iters);

void pbkdf2_sha256(void* psk, int len,
               void* pass, int passlen,
               void* salt, int saltlen, int iters);
