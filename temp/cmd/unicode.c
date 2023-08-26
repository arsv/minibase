#include <util.h>
#include <printf.h>
#include "unicode.h"

int deutf(char* buf, long len, uint* out)
{
	if(len < 1)
		return 0;

	int lead = (*buf & 0xFF);
	int need;
	uint code;

	if((lead & 0x80) == 0x00) {
		if(out) *out = lead;
		return 1;
	} else if((lead & 0xE0) == 0xC0) {
		code = lead & 0x1F;
		need = 2;
	} else if((lead & 0xF0) == 0xE0) {
		code = lead & 0x0F;
		need = 3;
	} else if((lead & 0xF8) == 0xF0) {
		code = lead & 0x07;
		need = 4;
	} else {
		return -1;
	}

	for(int i = 1; i < need; i++)
		if(i >= len)
			return 0; /* incomplete sequence */
		else if((buf[i] & 0xC0) != 0x80)
			return -i; /* malformed sequence */
		else
			code = (code << 6) | (buf[i] & 0x3F);

	if(out) *out = code;
	return need;
}

static int is_utf_cont_byte(char c)
{
	return ((c & 0xC0) == 0x80);
}

/* Both skip_ functions step over a single character (codepoint) and
   return its length in bytes. Length is never negative. Right step
   may return 0 if asked to step over an incomplete sequence at the
   end of the buffer. */

long skip_right(char* buf, long len)
{
	if(len < 1)
		return 0;

	int ul = deutf(buf, len, NULL);

	if(ul >= 0)
		return ul;
	else
		return -ul;
}

long skip_left(char* buf, long len)
{
	char* ptr = buf + len;
	char* end = ptr;

	while(ptr > buf)
		if(!is_utf_cont_byte(*(--ptr)))
			break;

	return end - ptr;
}

/* Jump over a word (as in, a sequence of non-space characters). */

static int isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || !c);
}

long skip_left_until_space(char* buf, long len)
{
	long skip;
	long ret = 0;

	while((skip = skip_left(buf, len)) > 0) {
		if(!isspace(buf[len - skip]))
			break;
		ret += skip;
		len -= skip;
	}

	while((skip = skip_left(buf, len)) > 0) {
		if(isspace(buf[len - skip]))
			break;
		ret += skip;
		len -= skip;
	}

	return ret;
}

/* To handle (prevent) wrapping, we need to keep track of how much visual
   space given UTF-8 string is expected to take on the screen. These calls
   are also used to handle cursor positioning.

   The tables below are grossly incomplete and only cover the most popular
   unicode ranges. But it's not like cmd is expected to need them much,
   or run on terminals with good Unicode support for that matter. */

static int viswi_page_03(int c)
{
	if(c >= 0x0300 && c <= 0x036F) return 0; /* generic combining stuff */
	return 1;
}

static int viswi_page_20(int c)
{
	if(c >= 0x20D0 && c <= 0x20EF) return 0; /* generic combining stuff */
	return 1;
}

static int viswi_page_11(int c)
{
	if(c >= 0x1100 && c <= 0x115F) return 2; /* Hangul double-width */
	if(c >= 0x1160 && c <= 0x11F9) return 0; /* Hangul combining */
	return 1;
}

static int viswi_page_FE(int c)
{
	if(c >= 0xFE20 && c <= 0xFE23) return 0; /* Combining ligatures */
	return 1;
}

static int viswi_page_FF(int c)
{
	if(c >= 0xFFFE && c <= 0xFFFF) return 0; /* BOM or invalid */
	return 1;
}

static int glyph_visual_width(uint c)
{
	int page = (c >> 8);

	if(page <= 0x02) return 1;
	if(page == 0x03) return viswi_page_03(c);
	if(page == 0x11) return viswi_page_11(c);
	if(page == 0x20) return viswi_page_20(c);
	if(page == 0xFE) return viswi_page_FE(c);
	if(page == 0xFF) return viswi_page_FF(c);
	if(page <= 0x2D) return 1;

	/* Larget CJK subsets */

	if(c >= 0x2E80 && c <= 0x3098) return 2;
	if(c >= 0x309D && c <= 0x4DB5) return 2;
	if(c >= 0x4E00 && c <= 0x9FC3) return 2;
	if(c >= 0xA000 && c <= 0xA4C6) return 2;

	/* Everything else presumed to be width 1 */

	return 1;
}

long visual_width(char* buf, long len)
{
	char* ptr = buf;
	char* end = buf + len;
	uint code;
	long skip;
	long width = 0;

	while(ptr < end) {
		if(!(skip = deutf(ptr, end - ptr, &code)))
			break;
		if(skip < 0) {
			width++;
			ptr += -skip;
		} else {
			width += glyph_visual_width(code);
			ptr += skip;
		}
	}

	return width;
}

/* Visual skip functions jump over clusters of combining characters
   about the same way cursor is expected to in a terminal. */

long skip_right_visually(char* buf, long len, long vstep)
{
	char* ptr = buf;
	char* end = buf + len;

	long skip;
	int vskip = 0;
	long ret = 0;

	while(ptr < end) {
		if(!(skip = skip_right(ptr, end - ptr)))
			break;

		if(skip < 0) {
			skip = -skip;
			vskip += 1;
		} else {
			vskip += visual_width(ptr, skip);
		}

		if(vskip > vstep)
			break;

		ptr += skip;
		ret += skip;

		if(vskip == vstep)
			break;
	}

	return ret;
}

long skip_left_visually(char* buf, long len, long vstep)
{
	char* ptr = buf + len;

	long skip;
	int vskip = 0;
	long ret = 0;

	while(ptr > buf) {
		if(!(skip = skip_left(buf, ptr - buf)))
			break;
		if(skip < 0) {
			skip = -skip;
			vskip += 1;
		} else {
			vskip += visual_width(ptr - skip, skip);
		}

		if(ptr - skip < buf)
			break;

		ptr -= skip;
		ret += skip;

		if(vskip >= vstep)
			break;
	}

	return ret;
}
