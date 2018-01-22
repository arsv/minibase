#include <bits/types.h>

void PRF480(uint8_t out[60], uint8_t key[32], char* str,
            uint8_t mac1[6], uint8_t mac2[6],
            uint8_t nonce1[32], uint8_t nonce2[32]);

void make_mic(uint8_t mic[16], uint8_t kck[16], char* buf, int len);
int check_mic(uint8_t mic[16], uint8_t kck[16], char* buf, int len);
int unwrap_key(uint8_t kek[16], char* buf, int len);
