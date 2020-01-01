#include <bits/types.h>
#include <bits/errno.h>
#include <string.h>
#include <cdefs.h>
#include <lzma.h>

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
/* not really states */
#define STATE_INVALID          13
#define STATE_INITIAL          14

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

struct private {
	struct lzma lz;

	int state;
	int pos_state;
	int error;

	uint32_t code;
	uint32_t range;

	int rep[4];

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
#define PZ struct private* pz

static inline struct private* private(struct lzma* lz)
{
	return (struct private*)(lz);
}

static inline struct lzma* public(struct private* pz)
{
	return (struct lzma*)(pz);
}

static void error(PZ, int err)
{
	pz->error = err;
}

static byte byte_error(PZ, int err)
{
	error(pz, err);
	return 0x00;
}

static int state_error(PZ, int err)
{
	error(pz, err);
	return STATE_INVALID;
}

static void put_byte(PZ, byte c)
{
	struct lzma* lz = public(pz);

	byte* ptr = lz->dstptr;
	byte* end = lz->dstend;

	if(ptr >= end)
		return error(pz, LZMA_OUTPUT_OVER);

	pz->pos_state = (pz->pos_state + 1) & 3;

	*ptr++ = c;
	lz->dstptr = ptr;
}

static byte get_byte(PZ)
{
	struct lzma* lz = public(pz);

	byte* ptr = lz->srcptr;
	byte* end = lz->srcend;

	if(ptr >= end)
		return byte_error(pz, LZMA_INPUT_OVER);

	lz->srcptr = ptr + 1;

	byte c = *ptr;

	return c;
}

static byte peek_back(PZ, uint i)
{
	struct lzma* lz = public(pz);

	byte* buf = lz->dstbuf;
	byte* ptr = lz->dstptr;

	byte* at = ptr - i - 1;

	if(at < buf || at >= ptr) {
		if(i) byte_error(pz, LZMA_INVALID_REF); /* ??? */
		return 0x00;
	}

	return *at;
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

static void check_buffers(LZ)
{
	struct private* pz = private(lz);

	void* srcbuf = lz->srcbuf;
	void* srcptr = lz->srcptr;
	void* srcend = lz->srcend;

	if(!srcbuf || !srcptr || !srcend)
		return error(pz, LZMA_INPUT_OVER);

	void* dstbuf = lz->dstbuf;
	void* dstptr = lz->dstptr;
	void* dstend = lz->dstend;

	if(!dstbuf || !dstptr || !dstend)
		return error(pz, LZMA_OUTPUT_OVER);

	if(pz->state != STATE_INITIAL)
		return;

	uint32_t code = 0;

	for(int i = 0; i < 4; i++)
		code = (code << 8) | get_byte(pz);

	pz->code = code;
	pz->state = STATE_LIT_LIT;
	pz->range = 0xFFFFFFFF;
}

static void init_private(PZ)
{
	pz->error = 0;
	pz->state = STATE_INITIAL;
	pz->pos_state = 0;
	/* these two will be set in check_buffers on the first call() */
	// pz->code = ...;
	// pz->range = 0xFFFFFFFF;

	memzero(pz->rep, sizeof(pz->rep));

	init_probs(BMD(pz->bit1));
	init_probs(BMS(pz->bit2));
	init_probs(BMS(pz->bit3));
	init_probs(BMS(pz->bit4));
	init_probs(BMS(pz->bit5));
	init_probs(BMD(pz->shrt));

	init_probs(BMD(pz->literal));
	init_probs(BMD(pz->dislot));
	init_probs(BMS(pz->dispec));
	init_probs(BMS(pz->align));

	init_lenmodel(&pz->matchlen);
	init_lenmodel(&pz->replen);
}

static void normalize(PZ)
{
	if(pz->range > 0x00FFFFFFU)
		return;

	pz->range <<= 8;
	pz->code = (pz->code << 8) | get_byte(pz);
}

static int dec_bit(PZ, bitmodel* bm)
{
	uint32_t probability = bm->probability;
	uint32_t bound = (pz->range >> 11) * probability;
	uint bit;

	if(pz->code < bound) {
		pz->range = bound;
		bm->probability += ((1<<11) - probability) >> 5;
		bit = 0;
	} else {
		pz->range -= bound;
		pz->code -= bound;
		bm->probability -= (probability >> 5);
		bit = 1;
	}

	normalize(pz);

	return bit;
}

static uint dec_direct(PZ, uint limit)
{
	unsigned i, ret = 0;

	for(i = limit; i > 0; i--) {
		pz->range >>= 1;
		ret <<= 1;

		if(pz->code >= pz->range) {
			pz->code -= pz->range;
			ret |= 1;
		}

		normalize(pz);
	}

	return ret;
}

static int range_check(PZ, uint i, uint size)
{
	if(i < size)
		return 0;

	error(pz, LZMA_RANGE_CHECK);

	return -1;
}

static uint dec_tree(PZ, bitmodel bma[], uint bms, uint n)
{
	uint i, ret = 1;

	for(i = 0; i < n; i++) {
		if(range_check(pz, ret, bms))
			return 0;
		ret = (ret << 1) | dec_bit(pz, &bma[ret]);
	}

	return ret - (1 << n);
}

static uint dec_rtree(PZ, bitmodel bma[], uint bms, uint n)
{
	uint sym = dec_tree(pz, bma, bms, n);
	uint ret = 0;

	for(uint i = 0; i < n; i++) {
		ret = (ret << 1) | (sym & 1);
		sym >>= 1;
	}

	return ret;
}

static uint dec_match(PZ, bitmodel bma[], uint bms, uint mbyte)
{
	uint i, ret = 1;

	for(i = 0; i < 8; i++) {
		uint matchbit = (mbyte >> (7-i)) & 1;
		uint off = ret + (matchbit << 8) + 0x100;

		if(range_check(pz, off, bms))
			return 0;

		uint bit = dec_bit(pz, &bma[off]);

		ret = (ret << 1) | bit;

		if(matchbit == bit)
			continue;

		while(ret < 0x100) {
			if(range_check(pz, ret, bms))
				return 0;

			ret = (ret << 1) | dec_bit(pz, &bma[ret]);
		}

		break;
	}

	return ret & 0xFF;
}

uint dec_length(PZ, lenmodel* lm, uint pstate)
{
	if(!dec_bit(pz, &lm->choice1))
		return dec_tree(pz, BMS(lm->low[pstate]), 3);
	if(!dec_bit(pz, &lm->choice2))
		return (1<<3) + dec_tree(pz, BMS(lm->mid[pstate]), 3);

	return (1<<3) + (1<<3) + dec_tree(pz, BMS(lm->high), 8);
}

static int is_lit(int state)
{
	return (state <= STATE_SHORTREP_LIT);
}

static int inflate_literal(PZ, int state)
{
	byte c;

	int lstate = peek_back(pz, 0) >> 5;

	if(range_check(pz, lstate, ARRAY_SIZE(pz->literal)))
		return state;

	bitmodel* bma = pz->literal[lstate];
	uint bms = ARRAY_SIZE(pz->literal[lstate]);

	if(is_lit(state))
		c = dec_tree(pz, bma, bms, 8);
	else
		c = dec_match(pz, bma, bms, peek_back(pz, pz->rep[0]));

	put_byte(pz, c);

	if(state <= STATE_SHORTREP_LIT_LIT)
		return STATE_LIT_LIT;
	else if(state <= STATE_LIT_SHORTREP)
		return state - 3;
	else
		return state - 6;
}

static int repeat_from_dict(PZ, int state, uint rep, uint n)
{
	uint i;

	for(i = 0; i < n; i++)
		put_byte(pz, peek_back(pz, rep));

	return state;
}

static int inflate_match(PZ, int state, int pstate)
{
	uint rep;

	pz->rep[3] = pz->rep[2];
	pz->rep[2] = pz->rep[1];
	pz->rep[1] = pz->rep[0];

	uint len = 2 + dec_length(pz, &pz->matchlen, pstate);
	int dstate = len - 2 < DSTATES - 1 ? len - 2 : DSTATES - 1;
	int dislot = dec_tree(pz, BMS(pz->dislot[dstate]), 6);

	if(dislot < DIST_MODEL_START) {
		rep = dislot;
	} else {
		int limit = (dislot >> 1) - 1;
		rep = (2 + (dislot & 1)) << limit;

		if(dislot < DIST_MODEL_END) {
			uint off = rep - dislot;
			uint bms = ARRAY_SIZE(pz->dispec) - off; /* ? */
			bitmodel* bma = pz->dispec + off;

			if(range_check(pz, off, ARRAY_SIZE(pz->dispec)))
				return state_error(pz, LZMA_RANGE_CHECK);

			int tree = dec_rtree(pz, bma, bms, limit);
			rep += tree;
		} else {
			int dbits = dec_direct(pz, limit - ALIGN_BITS);
			int rtree = dec_rtree(pz, BMS(pz->align), ALIGN_BITS);
			dbits <<= ALIGN_BITS;
			rep = rep | dbits | rtree;

			if(rep == 0xFFFFFFFF) {
				if(len != 2)
					return state_error(pz, LZMA_RANGE_CHECK);

				return state_error(pz, LZMA_STREAM_END);
			}
		}
	}

	pz->rep[0] = rep;

	state = is_lit(state) ? STATE_LIT_MATCH : STATE_NONLIT_MATCH;

	return repeat_from_dict(pz, state, rep, len);
}

static int inflate_shortrep(PZ, int state)
{
	state = is_lit(state) ? STATE_LIT_SHORTREP : STATE_NONLIT_REP;

	return repeat_from_dict(pz, state, pz->rep[0], 1);
}

static int inflate_longrep(PZ, int state, int pstate, int n)
{
	int rep = pz->rep[n];

	if(n > 2) pz->rep[3] = pz->rep[2];
	if(n > 1) pz->rep[2] = pz->rep[1];
	if(n > 0) pz->rep[1] = pz->rep[0];

	pz->rep[0] = rep;

	uint len = 2 + dec_length(pz, &pz->replen, pstate);

	state = is_lit(state) ? STATE_LIT_LONGREP : STATE_NONLIT_REP;

	return repeat_from_dict(pz, state, rep, len);
}

int lzma_inflate(LZ)
{
	struct private* pz = private(lz);
	int err;

	check_buffers(lz);

	while(!(err = pz->error)) {
		int p = pz->pos_state;
		int s = pz->state;

		if(range_check(pz, s, 12))
			continue; /* actually abort */
		if(range_check(pz, p, 4))
			continue; /* ditto */

		if(!dec_bit(pz, &pz->bit1[s][p])) {                 /* 0....  */
			s = inflate_literal(pz, s);
		} else if(!dec_bit(pz, &pz->bit2[s])) {             /* 10.... */
			s = inflate_match(pz, s, p);
		} else if(!dec_bit(pz, &pz->bit3[s])) {             /* 110... */
			if(!dec_bit(pz, &pz->shrt[s][p]))           /* 1100.. */
				s = inflate_shortrep(pz, s);
			else                                        /* 1101.. */
				s = inflate_longrep(pz, s, p, 0);
		} else {                                            /* 111... */
			if(!dec_bit(pz, &pz->bit4[s]))              /* 1110.. */
				s = inflate_longrep(pz, s, p, 1);
			else if(!dec_bit(pz, &pz->bit5[s]))         /* 11110. */
				s = inflate_longrep(pz, s, p, 2);
			else                                        /* 11111. */
				s = inflate_longrep(pz, s, p, 3);
		}

		pz->state = s;

		if(lz->srcptr > lz->srchwm)
			return LZMA_NEED_INPUT;
		if(lz->dstptr > lz->dsthwm)
			return LZMA_NEED_OUTPUT;
	}

	return err;
}

struct lzma* lzma_create(void* buf, int len)
{
	struct private* pz = buf;
	struct lzma* lz = public(pz);

	if(len < ssizeof(*pz))
		return NULL;

	memzero(lz, sizeof(lz));

	init_private(pz);

	return public(pz);
}
