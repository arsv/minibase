#include <sys/file.h>
#include <sys/mman.h>
#include <sys/creds.h>
#include <sys/signal.h>
#include <string.h>
#include <main.h>
#include <util.h>

/* The code below is based mostly on lzip by Antonio Diaz Diaz,
   adapted to minibase style and translated to plain C.

   This is a temporary tool written mostly to learn how lzip
   decompression works. */

ERRTAG("lunzip");

#define STATES 12
#define PSTATES 4
#define DSTATES 4
#define DISLOTS 64
#define LCONTEXT 8

#define ALIGN_BITS 4
#define DIST_MODEL_START 4
#define DIST_MODEL_END 14

#define STATE_LIT_LIT           0
#define STATE_MATCH_LIT_LIT     1
#define STATE_REP_LIT_LIT       2
#define STATE_SHORTREP_LIT_LIT  3
#define STATE_MATCH_LIT         4
#define STATE_REP_LIT           5
#define STATE_SHORTREP_LIT      6
#define STATE_LIT_MATCH         7
#define STATE_LIT_LONGREP       8
#define STATE_LIT_SHORTREP      9
#define STATE_NONLIT_MATCH     10
#define STATE_NONLIT_REP       11
#define END_OF_STREAM          -1

typedef struct {
	uint32_t probability;
} bitmodel;

typedef struct {
	bitmodel choice1;
	bitmodel choice2;
	bitmodel low[PSTATES][8];
	bitmodel mid[PSTATES][8];
	bitmodel high[256];
} lenmodel;

static struct bitmodel {
	bitmodel literal[LCONTEXT][0x300];
	bitmodel match[STATES][PSTATES];
	bitmodel rep[STATES];
	bitmodel rep0[STATES];
	bitmodel rep1[STATES];
	bitmodel rep2[STATES];
	bitmodel len[STATES][PSTATES];
	bitmodel dislot[DSTATES][DISLOTS];
	bitmodel dispec[115];
	bitmodel align[16];
} bm;

static struct lenmodel {
	lenmodel matchlen;
	lenmodel replen;
} lm;

static struct input {
	char* name;
	int fd;
	int mode;
	off_t size;
	off_t done;

	byte* buf;
	uint ptr;
	uint end;
	uint len;

	uint64_t pos;
} in;

static struct output {
	int fd;

	byte* buf;
	uint ptr;
	uint len;
	uint over;
	uint64_t pos;

	uint32_t crc;
	uint32_t crctbl[256];
} out;

static struct lzma {
	uint32_t code;
	uint32_t range;

	int rep[4];
} lz;

static void flush_out(void)
{
	int wr;
	uint i, n = out.ptr;
	uint crc = out.crc;

	for(i = 0; i < n; i++) {
		uint idx = (crc ^ out.buf[i]) & 0xFF;
		crc = out.crctbl[idx] ^ (crc >> 8);
	}

	out.crc = crc;

	if((wr = writeall(out.fd, out.buf, out.ptr)) < 0)
		fail("write", NULL, wr);

	out.ptr = 0;
}

static void put_byte(byte c)
{
	if(out.ptr >= out.len)
		fail("overflow", NULL, 0);

	out.buf[out.ptr++] = c;
	out.pos++;

	if(out.ptr < out.len)
		return;

	flush_out();
	out.over = 1;
}

static void read_page(void)
{
	int rd;
	byte* buf = in.buf;
	int len = 4096;

	if((rd = sys_read(in.fd, buf, len)) < 0)
		fail(NULL, in.name, rd);

	in.end = rd;
}

static void read_next(void)
{
	in.done += in.ptr;

	if(in.done >= in.size)
		fail("attemp to read past EOF", NULL, 0);

	if(in.ptr >= in.end) {
		in.ptr = 0;
		in.end = 0;
	}

	byte* buf = in.buf + in.end;
	int len = in.len - in.end;
	int rd;

	if((rd = sys_read(in.fd, buf, len)) < 0)
		fail(NULL, in.name, rd);
	else if(!rd)
		fail("unexpected EOF reading", in.name, 0);

	in.end += rd;

}

static byte get_byte(void)
{
	if(in.ptr >= in.end)
		read_next();
	if(in.ptr >= in.end)
		fail("attempt to read past EOF", NULL, 0);

	in.pos++;

	return in.buf[in.ptr++];
}

static byte peek_back(uint i)
{
	if(i < out.pos)
		return out.buf[out.pos - i - 1];
	if(i < out.len && out.over)
		return out.buf[out.len - i - 1];

	return 0;
}

#define init_prob(a) init_probabilities((bitmodel*)a, ARRAY_SIZE(a))

static void init_probabilities(void* ptr, int size)
{
	bitmodel* bmp = (bitmodel*)ptr;
	int i, n = size / sizeof(*bmp);

	for(i = 0; i < n; i++)
		bmp[i].probability = (1<<10);
}

static void init_lzma_state(void)
{
	uint32_t code = 0;

	(void)get_byte();

	for(int i = 0; i < 4; i++)
		code = (code << 8) | get_byte();

	lz.code = code;
	lz.range = 0xFFFFFFFFU;

	init_probabilities(&bm, sizeof(bm));
	init_probabilities(&lm, sizeof(lm));
}

static void normalize(void)
{
	if(lz.range > 0x00FFFFFFU)
		return;

	lz.range <<= 8;
	lz.code = (lz.code << 8) | get_byte();
}

static int dec_bit(bitmodel* bm)
{
	uint32_t probability = bm->probability;
	uint32_t bound = (lz.range >> 11) * probability;
	uint bit;

	if(lz.code < bound) {
		lz.range = bound;
		bm->probability += ((1<<11) - probability) >> 5;
		bit = 0;
	} else {
		lz.range -= bound;
		lz.code -= bound;
		bm->probability -= (probability >> 5);
		bit = 1;
	}

	normalize();

	return bit;
}

static uint dec_direct(uint limit)
{
	unsigned i, ret = 0;

	for(i = limit; i > 0; i--) {
		lz.range >>= 1;
		ret <<= 1;

		if(lz.code >= lz.range) {
			lz.code -= lz.range;
			ret |= 1;
		}

		normalize();
	}

	return ret;
}

/* Since this is a temp implementation anyway, full range checks for all
   bitmodel arrays. Extremely important when debugging the code below.
   Using a wrong probability slot does not cause errors right at the point,
   but wreaks the subsequent bitstream. A slight overshot in one of bm.*
   arrays is vely likely to hit another array there.

   The check is not done in dec_bit because of 2d arrays. */

static void range_check(uint i, uint size)
{
	if(i >= size) sys_kill(sys_getpid(), SIGABRT);
}

#define BMS(a) a, ARRAY_SIZE(a)

static uint dec_tree(bitmodel bma[], uint bms, uint n)
{
	uint i, ret = 1;

	for(i = 0; i < n; i++) {
		range_check(ret, bms);
		ret = (ret << 1) | dec_bit(&bma[ret]);
	}

	return ret - (1 << n);
}

static uint dec_rtree(bitmodel bma[], uint bms, uint n)
{
	uint sym = dec_tree(bma, bms, n);
	uint ret = 0;

	for(uint i = 0; i < n; i++) {
		ret = (ret << 1) | (sym & 1);
		sym >>= 1;
	}

	return ret;
}

uint dec_match(bitmodel bma[], uint bms, uint mbyte)
{
	uint i, ret = 1;

	for(i = 0; i < 8; i++) {
		uint matchbit = (mbyte >> (7-i)) & 1;
		uint off = ret + (matchbit << 8) + 0x100;
		range_check(off, bms);
		uint bit = dec_bit(&bma[off]);

		ret = (ret << 1) | bit;

		if(matchbit == bit)
			continue;

		while(ret < 0x100) {
			range_check(ret, bms);
			ret = (ret << 1) | dec_bit(&bma[ret]);
		}

		break;
	}

	return ret & 0xFF;
}

uint dec_length(lenmodel* lm, uint pstate)
{
	if(!dec_bit(&lm->choice1))
		return dec_tree(BMS(lm->low[pstate]), 3);
	if(!dec_bit(&lm->choice2))
		return (1<<3) + dec_tree(BMS(lm->mid[pstate]), 3);

	return (1<<3) + (1<<3) + dec_tree(BMS(lm->high), 8);
}

/* Bulk stream decoding stage. The stream consists of literal bytes
   and several kinds of back-references to already decoded data
   up to dictsize bytes back into the output buffer (aka the dictionary).
   The difference between various back-reference is mostly in the way
   offset and count are encoded in the bitstream.

   The variation in the sequence of literals and references is used
   to predict subsequent bit values. The sequence is tracked in $state
   which is then used to key certain probability arrays. */

static int is_lit(int state)
{
	return (state <= STATE_SHORTREP_LIT);
}

static int inflate_literal(int state)
{
	byte c;

	int lstate = peek_back(0) >> 5;
	range_check(lstate, ARRAY_SIZE(bm.literal));

	bitmodel* bma = bm.literal[lstate];
	uint bms = ARRAY_SIZE(bm.literal[lstate]);

	if(is_lit(state))
		c = dec_tree(bma, bms, 8);
	else
		c = dec_match(bma, bms, peek_back(lz.rep[0]));

	put_byte(c);

	if(state <= STATE_SHORTREP_LIT_LIT)
		return STATE_LIT_LIT;
	else if(state <= STATE_LIT_SHORTREP)
		return state - 3;
	else
		return state - 6;
}

static int repeat_from_dict(int state, uint rep, uint n)
{
	uint i;

	for(i = 0; i < n; i++)
		put_byte(peek_back(rep));

	return state;
}

static int inflate_match(int state, int pstate)
{
	uint rep;

	lz.rep[3] = lz.rep[2];
	lz.rep[2] = lz.rep[1];
	lz.rep[1] = lz.rep[0];

	uint len = 2 + dec_length(&lm.matchlen, pstate);
	int dstate = len - 2 < DSTATES - 1 ? len - 2 : DSTATES - 1;
	int dislot = dec_tree(BMS(bm.dislot[dstate]), 6);

	if(dislot < DIST_MODEL_START) {
		rep = dislot;
	} else {
		int limit = (dislot >> 1) - 1;
		rep = (2 + (dislot & 1)) << limit;

		if(dislot < DIST_MODEL_END) {
			uint off = rep - dislot;
			uint bms = ARRAY_SIZE(bm.dispec) - off; /* ? */
			bitmodel* bma = bm.dispec + off;
			range_check(off, ARRAY_SIZE(bm.dispec));
			int tree = dec_rtree(bma, bms, limit);
			rep += tree;
		} else {
			int dbits = dec_direct(limit - ALIGN_BITS);
			int rtree = dec_rtree(BMS(bm.align), ALIGN_BITS);
			dbits <<= ALIGN_BITS;
			rep = rep | dbits | rtree;

			if(rep != 0xFFFFFFFF)
				;
			else if(len != 2)
				fail("invalid EOF marker", NULL, 0);
			else
				return END_OF_STREAM;
		}
	}

	lz.rep[0] = rep;

	state = is_lit(state) ? STATE_LIT_MATCH : STATE_NONLIT_MATCH;

	return repeat_from_dict(state, rep, len);
}

static int inflate_shortrep(int state)
{
	state = is_lit(state) ? STATE_LIT_SHORTREP : STATE_NONLIT_REP;

	return repeat_from_dict(state, lz.rep[0], 1);
}

static int inflate_longrep(int state, int pstate, int n)
{
	int rep = lz.rep[n];

	if(n > 2) lz.rep[3] = lz.rep[2];
	if(n > 1) lz.rep[2] = lz.rep[1];
	if(n > 0) lz.rep[1] = lz.rep[0];

	lz.rep[0] = rep;

	uint len = 2 + dec_length(&lm.replen, pstate);

	state = is_lit(state) ? STATE_LIT_LONGREP : STATE_NONLIT_REP;

	return repeat_from_dict(state, rep, len);
}

static void inflate_data(void)
{
	int s = STATE_LIT_LIT; /* state */

	while(1) {
		int p = out.pos & 3; /* pstate, or pos_state */

		range_check(s, 12);
		range_check(p, 4);

		if(!dec_bit(&bm.match[s][p])) {                     /* 0....  */
			s = inflate_literal(s);
		} else if(!dec_bit(&bm.rep[s])) {                   /* 10.... */
			s = inflate_match(s, p);
			if(s < 0) break; /* end-of-stream */
		} else if(!dec_bit(&bm.rep0[s])) {                  /* 110... */
			if(!dec_bit(&bm.len[s][p]))                 /* 1100.. */
				s = inflate_shortrep(s);
			else                                        /* 1101.. */
				s = inflate_longrep(s, p, 0);
		} else {                                            /* 111... */
			if(!dec_bit(&bm.rep1[s]))                   /* 1110.. */
				s = inflate_longrep(s, p, 1);
			else if(!dec_bit(&bm.rep2[s]))              /* 11110. */
				s = inflate_longrep(s, p, 2);
			else                                        /* 11111. */
				s = inflate_longrep(s, p, 3);
		}
	}

	flush_out();
}

/* Lzip trailer, mostly for checking data integrity. Should be 20 bytes. */

static uint32_t get_word(void)
{
	uint32_t ret;

	ret  = get_byte();
	ret |= get_byte() << 8;
	ret |= get_byte() << 16;
	ret |= get_byte() << 24;

	return ret;
}

uint64_t get_long(void)
{
	uint32_t lo = get_word();
	uint32_t hi = get_word();

	uint64_t ret = hi;

	ret = (ret << 32) | lo;

	return ret;
}

static void check_trailing(void)
{
	uint32_t crc = get_word();
	uint64_t osize = get_long();
	uint64_t isize = get_long();

	out.crc ^= 0xFFFFFFFF;

	if(crc != out.crc)
		fail("wrong CRC", NULL, 0);
	if(osize != out.pos)
		fail("file size mismatch", NULL, 0);
	if(isize != in.pos)
		fail("compressed size mismatch", NULL, 0);
	if(in.ptr < in.end)
		fail("trailing garbage", NULL, 0);
}

/* Init stage. Read file header, verify it, allocate and initialize
   required temporary structures (dictionary, probabilities, rc decode
   state).

   Here dictionary also doubles as the output buffer. In most cases,
   it's large enough to serve as one, and for smaller files it may be
   large enough to contain the whole output.

   See also get_byte(), put_byte() and peek_back() above. */

static void prep_output_buf(uint size)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int ret;

	byte* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	out.len = size;
	out.buf = buf;
}

static void init_crc_table(void)
{
	uint i, c, k;
	static const uint mask[2] = { 0x00000000, 0xEDB88320U };

	for(i = 0; i < 256; i++) {
		c = i;

		for(k = 0; k < 8; k++)
			c = (c >> 1) ^ mask[c & 1];

		out.crctbl[i] = c;
	}

	out.crc = 0xFFFFFFFF;
}

static void prep_input_buf(void)
{
	uint maxbufsize = 1024*1024;
	uint size;

	if(in.size <= maxbufsize)
		size = in.size;
	else
		size = maxbufsize;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int ret;

	byte* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	in.buf = buf;
	in.len = size;
}

static void prep_decoder(void)
{
	int maxoneshot = 128*1024;

	prep_input_buf();

	if(in.size >= maxoneshot)
		read_page();
	else
		read_next();

	if(in.end < 6)
		fail("not a lzip archive:", in.name, 0);
	if(memcmp(in.buf, "LZIP\x01", 5))
		fail("not a lzip archive:", in.name, 0);

	byte dscode = in.buf[5];
	uint dictsize = 1 << (dscode & 0x1F);
	dictsize -= (dictsize/16) * ((dscode >> 5) & 7);
	in.ptr = 6;
	in.pos = 6;

	if(dictsize < (1<<12) || dictsize > (1<<29))
		fail("invalid dictonary size", NULL, 0);

	prep_output_buf(dictsize);
	init_crc_table();
	init_lzma_state();

	if(in.size >= maxoneshot)
		read_next();
}

/* File setup: "foo.bar.lz" gets unzipped into "foo.bar". */

static void open_out_explicit(char* name)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = in.mode;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	out.fd = fd;
}

static void open_out_desuffix(char* name)
{
	int nlen = strlen(name);

	if(nlen < 3 || strcmp(name + nlen - 3, ".lz"))
		fail("no .lz suffix:", name, 0);

	char buf[nlen];
	memcpy(buf, name, nlen - 3);
	buf[nlen-3] = '\0';

	return open_out_explicit(buf);
}

static void open_input(char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	in.name = name;
	in.fd = fd;
	in.size = st.size;
	in.mode = st.mode;
}

int main(int argc, char** argv)
{
	if(argc < 2)
		fail("too few arguments", NULL, 0);

	open_input(argv[1]);

	if(argc == 2)
		open_out_desuffix(argv[1]);
	else if(argc == 3)
		open_out_explicit(argv[2]);
	else
		fail("too many arguments", NULL, 0);

	prep_decoder();
	inflate_data();
	check_trailing();

	return 0;
}
