#include <sys/file.h>
#include <sys/mman.h>
#include <sys/creds.h>
#include <sys/signal.h>

#include <string.h>
#include <util.h>

#include "modprobe.h"

/* Lzip decode is simple enough to warrant embedding, and the ability
   to load compressed modules without relying on external processes
   is extremely important at initrd stage. Not as much as the pipe mode,
   but still having 10+ modules is not uncommon.
   
   See ../../temp/lzma/ for explanation of the code here. */

#define MAX_ZIP_SIZE 0x7FFFFE

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

struct input {
	char* name;
	byte* buf;
	uint ptr;
	uint len;
};

struct output {
	byte* buf;
	uint ptr;
	uint len;
};

struct lzma {
	struct top* ctx;

	int error;
	uint dictsize;

	uint32_t code;
	uint32_t range;

	int rep[4];

	struct input in;
	struct output out;

	bitmodel bit1[STATES][PSTATES];
	bitmodel bit2[STATES];
	bitmodel bit3[STATES];
	bitmodel bit4[STATES];
	bitmodel bit5[STATES];
	bitmodel shrt[STATES][PSTATES];

	bitmodel literal[LCONTEXT][0x300];
	bitmodel dislot[DSTATES][DISLOTS];
	bitmodel dispec[115];
	bitmodel align[16];

	lenmodel matchlen;
	lenmodel replen;
};

#define LZ struct lzma* lz

static void mark_error(const char* msg, LZ)
{
	lz->error = error(lz->ctx, msg, lz->in.name, 0);
}

static void warn_error(const char* msg, LZ, int err)
{
	lz->error = error(lz->ctx, msg, NULL, err);
}

static void put_byte(LZ, byte c)
{
	struct output* out = &lz->out;

	if(out->ptr >= out->len) {
		byte* oldbuf = out->buf;
		uint oldlen = out->len;
		uint newlen = oldlen + 2*PAGE;
		int ret, flags = MREMAP_MAYMOVE;

		void* newbuf = sys_mremap(oldbuf, oldlen, newlen, flags);

		if((ret = mmap_error(newbuf))) {
			warn_error("mremap", lz, ret);
			return;
		}

		out->buf = newbuf;
		out->len = newlen;
	}

	out->buf[out->ptr++] = c;
}

static byte get_byte(LZ)
{
	struct input* in = &lz->in;

	if(in->ptr >= in->len) {
		mark_error("EOF in", lz);
		return 0;
	}

	return in->buf[in->ptr++];
}

static byte peek_back(LZ, uint i)
{
	struct output* out = &lz->out;

	if(i >= out->ptr) {
		if(!i) return 0x00;
		mark_error("peek beyond SOF in", lz);
		return 0;
	} else if(i >= lz->dictsize) {
		mark_error("peek beyond dictsize in", lz);
		return 0;
	}

	return out->buf[out->ptr - i - 1];
}

#define BMS(a) a, ARRAY_SIZE(a)
#define BMD(a) (bitmodel*)a, ARRAY_SIZE(a)*ARRAY_SIZE(a[0])

static void init_probs(bitmodel bmp[], uint size)
{
	for(uint i = 0; i < size; i++)
		bmp[i].probability = (1<<10);
}

static void init_lenmodel(lenmodel* lm)
{
	init_probs(&lm->choice1, 1);
	init_probs(&lm->choice2, 1);

	init_probs(BMD(lm->low));
	init_probs(BMD(lm->mid));
	init_probs(BMS(lm->high));
}

static void init_lzma_state(LZ)
{
	uint32_t code = 0;

	(void)get_byte(lz);

	for(int i = 0; i < 4; i++)
		code = (code << 8) | get_byte(lz);

	lz->code = code;
	lz->range = 0xFFFFFFFFU;

	init_probs(BMD(lz->bit1));
	init_probs(BMS(lz->bit2));
	init_probs(BMS(lz->bit3));
	init_probs(BMS(lz->bit4));
	init_probs(BMS(lz->bit5));
	init_probs(BMD(lz->shrt));

	init_probs(BMD(lz->literal));
	init_probs(BMD(lz->dislot));
	init_probs(BMS(lz->dispec));
	init_probs(BMS(lz->align));

	init_lenmodel(&lz->matchlen);
	init_lenmodel(&lz->replen);
}

static void normalize(LZ)
{
	if(lz->range > 0x00FFFFFFU)
		return;

	lz->range <<= 8;
	lz->code = (lz->code << 8) | get_byte(lz);
}

static int dec_bit(LZ, bitmodel* bm)
{
	uint32_t probability = bm->probability;
	uint32_t bound = (lz->range >> 11) * probability;
	uint bit;

	if(lz->code < bound) {
		lz->range = bound;
		bm->probability += ((1<<11) - probability) >> 5;
		bit = 0;
	} else {
		lz->range -= bound;
		lz->code -= bound;
		bm->probability -= (probability >> 5);
		bit = 1;
	}

	normalize(lz);

	return bit;
}

static uint dec_direct(LZ, uint limit)
{
	unsigned i, ret = 0;

	for(i = limit; i > 0; i--) {
		lz->range >>= 1;
		ret <<= 1;

		if(lz->code >= lz->range) {
			lz->code -= lz->range;
			ret |= 1;
		}

		normalize(lz);
	}

	return ret;
}

static int range_check(LZ, uint i, uint size)
{
	if(i < size)
		return 0;

	mark_error("range check in", lz);

	return -1;
}

static uint dec_tree(LZ, bitmodel bma[], uint bms, uint n)
{
	uint i, ret = 1;

	for(i = 0; i < n; i++) {
		if(range_check(lz, ret, bms))
			return 0;
		ret = (ret << 1) | dec_bit(lz, &bma[ret]);
	}

	return ret - (1 << n);
}

static uint dec_rtree(LZ, bitmodel bma[], uint bms, uint n)
{
	uint sym = dec_tree(lz, bma, bms, n);
	uint ret = 0;

	for(uint i = 0; i < n; i++) {
		ret = (ret << 1) | (sym & 1);
		sym >>= 1;
	}

	return ret;
}

static uint dec_match(LZ, bitmodel bma[], uint bms, uint mbyte)
{
	uint i, ret = 1;

	for(i = 0; i < 8; i++) {
		uint matchbit = (mbyte >> (7-i)) & 1;
		uint off = ret + (matchbit << 8) + 0x100;

		if(range_check(lz, off, bms))
			return 0;

		uint bit = dec_bit(lz, &bma[off]);

		ret = (ret << 1) | bit;

		if(matchbit == bit)
			continue;

		while(ret < 0x100) {
			if(range_check(lz, ret, bms))
				return 0;

			ret = (ret << 1) | dec_bit(lz, &bma[ret]);
		}

		break;
	}

	return ret & 0xFF;
}

uint dec_length(LZ, lenmodel* lm, uint pstate)
{
	if(!dec_bit(lz, &lm->choice1))
		return dec_tree(lz, BMS(lm->low[pstate]), 3);
	if(!dec_bit(lz, &lm->choice2))
		return (1<<3) + dec_tree(lz, BMS(lm->mid[pstate]), 3);

	return (1<<3) + (1<<3) + dec_tree(lz, BMS(lm->high), 8);
}

static int is_lit(int state)
{
	return (state <= STATE_SHORTREP_LIT);
}

static int inflate_literal(LZ, int state)
{
	byte c;

	int lstate = peek_back(lz, 0) >> 5;

	if(range_check(lz, lstate, ARRAY_SIZE(lz->literal)))
		return state;

	bitmodel* bma = lz->literal[lstate];
	uint bms = ARRAY_SIZE(lz->literal[lstate]);

	if(is_lit(state))
		c = dec_tree(lz, bma, bms, 8);
	else
		c = dec_match(lz, bma, bms, peek_back(lz, lz->rep[0]));

	put_byte(lz, c);

	if(state <= STATE_SHORTREP_LIT_LIT)
		return STATE_LIT_LIT;
	else if(state <= STATE_LIT_SHORTREP)
		return state - 3;
	else
		return state - 6;
}

static int repeat_from_dict(LZ, int state, uint rep, uint n)
{
	uint i;

	for(i = 0; i < n; i++)
		put_byte(lz, peek_back(lz, rep));

	return state;
}

static int inflate_match(LZ, int state, int pstate)
{
	uint rep;

	lz->rep[3] = lz->rep[2];
	lz->rep[2] = lz->rep[1];
	lz->rep[1] = lz->rep[0];

	uint len = 2 + dec_length(lz, &lz->matchlen, pstate);
	int dstate = len - 2 < DSTATES - 1 ? len - 2 : DSTATES - 1;
	int dislot = dec_tree(lz, BMS(lz->dislot[dstate]), 6);

	if(dislot < DIST_MODEL_START) {
		rep = dislot;
	} else {
		int limit = (dislot >> 1) - 1;
		rep = (2 + (dislot & 1)) << limit;

		if(dislot < DIST_MODEL_END) {
			uint off = rep - dislot;
			uint bms = ARRAY_SIZE(lz->dispec) - off; /* ? */
			bitmodel* bma = lz->dispec + off;

			if(range_check(lz, off, ARRAY_SIZE(lz->dispec)))
				return END_OF_STREAM;

			int tree = dec_rtree(lz, bma, bms, limit);
			rep += tree;
		} else {
			int dbits = dec_direct(lz, limit - ALIGN_BITS);
			int rtree = dec_rtree(lz, BMS(lz->align), ALIGN_BITS);
			dbits <<= ALIGN_BITS;
			rep = rep | dbits | rtree;

			if(rep == 0xFFFFFFFF) {
				if(len != 2)
					mark_error("invalid EOF marker in", lz);

				return END_OF_STREAM;
			}
		}
	}

	lz->rep[0] = rep;

	state = is_lit(state) ? STATE_LIT_MATCH : STATE_NONLIT_MATCH;

	return repeat_from_dict(lz, state, rep, len);
}

static int inflate_shortrep(LZ, int state)
{
	state = is_lit(state) ? STATE_LIT_SHORTREP : STATE_NONLIT_REP;

	return repeat_from_dict(lz, state, lz->rep[0], 1);
}

static int inflate_longrep(LZ, int state, int pstate, int n)
{
	int rep = lz->rep[n];

	if(n > 2) lz->rep[3] = lz->rep[2];
	if(n > 1) lz->rep[2] = lz->rep[1];
	if(n > 0) lz->rep[1] = lz->rep[0];

	lz->rep[0] = rep;

	uint len = 2 + dec_length(lz, &lz->replen, pstate);

	state = is_lit(state) ? STATE_LIT_LONGREP : STATE_NONLIT_REP;

	return repeat_from_dict(lz, state, rep, len);
}

static int inflate_data(LZ)
{
	int s = STATE_LIT_LIT; /* state */

	while(1) {
		int p = lz->out.ptr & 3; /* pstate, or pos_state */

		if(range_check(lz, s, 12))
			break;
		if(range_check(lz, p, 4))
			break;

		if(!dec_bit(lz, &lz->bit1[s][p])) {                 /* 0....  */
			s = inflate_literal(lz, s);
		} else if(!dec_bit(lz, &lz->bit2[s])) {             /* 10.... */
			s = inflate_match(lz, s, p);
			if(s < 0) break; /* end-of-stream */
		} else if(!dec_bit(lz, &lz->bit3[s])) {             /* 110... */
			if(!dec_bit(lz, &lz->shrt[s][p]))           /* 1100.. */
				s = inflate_shortrep(lz, s);
			else                                        /* 1101.. */
				s = inflate_longrep(lz, s, p, 0);
		} else {                                            /* 111... */
			if(!dec_bit(lz, &lz->bit4[s]))              /* 1110.. */
				s = inflate_longrep(lz, s, p, 1);
			else if(!dec_bit(lz, &lz->bit5[s]))         /* 11110. */
				s = inflate_longrep(lz, s, p, 2);
			else                                        /* 11111. */
				s = inflate_longrep(lz, s, p, 3);
		}

		if(lz->error)
			return -EINVAL;
	}

	return 0;
}

/* Lzip trailer */

static uint32_t get_word(LZ)
{
	uint32_t ret;

	ret  = get_byte(lz);
	ret |= get_byte(lz) << 8;
	ret |= get_byte(lz) << 16;
	ret |= get_byte(lz) << 24;

	return ret;
}

uint64_t get_long(LZ)
{
	uint32_t lo = get_word(lz);
	uint32_t hi = get_word(lz);

	uint64_t ret = hi;

	ret = (ret << 32) | lo;

	return ret;
}

static uint32_t calc_output_crc(LZ)
{
	byte* buf = lz->out.buf;
	uint i, n = lz->out.ptr;

	static const uint mask[2] = { 0x00000000, 0xEDB88320U };
	uint32_t crc = 0xFFFFFFFF;
	uint32_t crctbl[256];

	for(i = 0; i < 256; i++) {
		uint c = i;

		for(uint k = 0; k < 8; k++)
			c = (c >> 1) ^ mask[c & 1];

		crctbl[i] = c;
	}

	crc = 0xFFFFFFFF;

	for(i = 0; i < n; i++) {
		uint idx = (crc ^ buf[i]) & 0xFF;
		crc = crctbl[idx] ^ (crc >> 8);
	}

	crc ^= 0xFFFFFFFF;

	return crc;
}

static int check_trailing(LZ)
{
	struct top* ctx = lz->ctx;
	char* name = lz->in.name;

	uint32_t expcrc = get_word(lz);
	uint64_t osize = get_long(lz);
	uint64_t isize = get_long(lz);

	if(lz->error) /* truncated file, at this point */
		return lz->error;

	uint32_t gotcrc = calc_output_crc(lz);

	if(gotcrc != expcrc)
		return error(ctx, "wrong CRC in", name, 0);
	if(osize != lz->out.ptr)
		return error(ctx, "file size mismatch in", name, 0);
	if(isize != lz->in.ptr)
		return error(ctx, "compressed size mismatch in", name, 0);
	if(lz->in.ptr < lz->in.len)
		return error(ctx, "trailing garbage in", name, 0);

	return 0;
}

/* Lzip header */

static int prep_output_buf(LZ, uint size)
{
	struct top* ctx = lz->ctx;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int ret;

	byte* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		return error(ctx, "mmap", NULL, ret);

	lz->out.len = size;
	lz->out.buf = buf;

	lz->dictsize = size;

	return 0;
}

static int prep_decoder(LZ)
{
	struct top* ctx = lz->ctx;
	struct input* in = &lz->in;
	int ret;

	if(in->len < 6 + 2 + 20)
		return error(ctx, "not a lzip archive:", in->name, 0);
	if(memcmp(in->buf, "LZIP\x01", 5))
		return error(ctx, "not a lzip archive:", in->name, 0);

	byte dscode = in->buf[5];
	uint dictsize = 1 << (dscode & 0x1F);
	dictsize -= (dictsize/16) * ((dscode >> 5) & 7);

	in->ptr = 6;

	if(dictsize < (1<<12) || dictsize > (1<<29))
		return error(ctx, "invalid dictonary size in", in->name, 0);

	if((ret = prep_output_buf(lz, dictsize)) < 0)
		return ret;

	init_lzma_state(lz);

	return 0;
}

static int open_input(LZ, char* name)
{
	struct top* ctx = lz->ctx;
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0) {
		return error(ctx, NULL, name, fd);
	} if((ret = sys_fstat(fd, &st)) < 0) {
		ret = error(ctx, "stat", name, ret);
		goto out;
	} if(st.size > MAX_ZIP_SIZE) {
		ret = error(ctx, NULL, name, -E2BIG);
		goto out;
	}

	int prot = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if((ret = mmap_error(buf))) {
		ret = error(ctx, "mmap", name, ret);
		goto out;
	}

	lz->in.name = name;
	lz->in.buf = buf;
	lz->in.ptr = 0;
	lz->in.len = st.size;

	ret = 0;
out:
	sys_close(fd);

	return ret;
}

static void unmap_lz_buf(void* buf, uint len)
{
	int ret;

	if((ret = sys_munmap(buf, len)) < 0)
		warn("unmap", NULL, ret);
}

static void move_out_buf(LZ, struct mbuf* mb)
{
	mb->buf = lz->out.buf;
	mb->len = lz->out.ptr;
	mb->full = lz->out.len;
	mb->tried = 1;
}

int lunzip(CTX, struct mbuf* mb, char* name)
{
	struct lzma lzma, *lz = &lzma;
	int ret;

	memzero(lz, sizeof(*lz));
	
	lz->ctx = ctx;

	if((ret = open_input(lz, name)) < 0)
		return ret;

	if((ret = prep_decoder(lz)) < 0)
		;
	else if((ret = inflate_data(lz)) < 0)
		;
	else ret = check_trailing(lz);

	if(ret >= 0)
		move_out_buf(lz, mb);
	else
		unmap_lz_buf(lz->out.buf, lz->out.len);

	unmap_lz_buf(lz->in.buf, lz->in.len);

	return ret;
}
