#define LZMA_SIZE 30000

#define LZMA_STREAM_END  1
#define LZMA_NEED_INPUT  2
#define LZMA_NEED_OUTPUT 3

#define LZMA_OUTPUT_OVER 4
#define LZMA_INPUT_OVER  5
#define LZMA_INVALID_REF 6
#define LZMA_RANGE_CHECK 7

struct lzma {
	void* srcbuf;
	void* srcptr;
	void* srchwm;
	void* srcend;

	void* dstbuf;
	void* dstptr;
	void* dsthwm;
	void* dstend;

	byte private[];
};

struct lzma* lzma_create(void* buf, int len);
void lzma_prepare(struct lzma* lz);
int lzma_inflate(struct lzma* lz);
