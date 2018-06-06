#include <bits/types.h>

void PRF480(byte out[60], byte key[32], char* str,
            byte mac1[6], byte mac2[6],
            byte nonce1[32], byte nonce2[32]);

void make_mic(byte mic[16], byte kck[16], void* buf, int len);
int check_mic(byte mic[16], byte kck[16], void* buf, int len);
int unwrap_key(byte kek[16], void* buf, int len);
