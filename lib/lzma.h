/* Core part of LZMA deflate algorithm, in a form that can be used
   for embedding into various tools that need native lz support.

   The caller supplies input (src) and output (dst) buffers in the
   structure below. The LZMA code then proceedes to advance srcptr
   and dstptr as it reads the input and produces output. Once either
   of those pointers exceeds respective high-water marks (srchwm or
   dsthwm), inflate() returns LZMA_NEED_INPUT or LZMA_NEED_OUTPUT
   to the caller which is then expected to read/flush the data,
   update the pointers and call inflate() again.

   LZMA code can and will overshot hwm-s, by several bytes in case of
   input and by up to ~300 bytes for output. This is expected. The areas
   between hwn and end pointers allow for soft-braking the algorithm,
   making it restartable without much impact on the performance.
   The caller should aim at having around a PAGE between respective hwm
   and end pointers prior to any inflate() call.

   Hitting either of the end pointers is a hard non-recoverable error
   indicated by LZMA_INPUT_OVER or LZMA_OUTPUT_OVER. Getting those codes
   means the process cannot be restarted anymore.

   LZMA streams typically contain back-references, which in this setup
   would be counted back from dstptr. It is up to the caller to figure
   out dictsize for the stream, and keep at least that much decoded data
   in memory between dstbuf and dstptr when flushing output.
   Any back-reference past dstbuf causes LZMA_INVALID_REF return which
   is not recoverable.

   The private[] part of the structure is pretty involved and does not
   need to be exposed to the caller. See `struct private` in lzma.c.
   It is, however, must be kept intact between calls to inflate() through
   a continuous input stream. The caller is expected to reserve a buffer
   of at least LZMA_SIZE, and use lzma_create() to turn that into a usable
   `struct lzma`. */

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
int lzma_inflate(struct lzma* lz);
