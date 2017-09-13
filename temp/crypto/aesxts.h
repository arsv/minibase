#include <bits/ints.h>

struct vec {
	char* tag;
	uint8_t key[16];
	uint8_t ptx[512];
	uint8_t ctx[512];
	uint64_t lba;
};

extern const struct vec vec20;
extern const struct vec vec21;
extern const struct vec vec25;
