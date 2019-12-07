#include <sys/time.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <format.h>
#include <printf.h>
#include <string.h>
#include <output.h>
#include <time.h>
#include <util.h>
#include <main.h>

ERRTAG("cal");

#define OPTS "y"
#define OPT_y   (1<<0)
#define SET_mon (1<<8)

#define TAG 88

struct row {
	unsigned char cell[7][1+13+1];
};

struct top {
	int opts;

	int year;
	int month;
	int day;
	int weekday;

	struct {
		int month;
		int day;
	} mark;

	struct bufout bo;
};

char outbuf[4096];

#define CTX struct top* ctx __unused

static const char lcmonth[12][3] = {
	"jan", "feb", "mar", "apr", "may", "jun",
	"jul", "aug", "sep", "oct", "nov", "dec"
};
static const char ucmonth[12][3] = {
	"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
	"JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};
static const char weekdays[7][3] = {
	"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"
};
static const char months[12][12] = {
	"January", "February", "March",
	"April", "May", "June",
	"July", "August", "September",
	"October", "November", "December"
};
static const uint8_t mthdays[] = {
	0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* See date_time.c for detailed explanation of timezone file
   format and related routines. This is a simplified version
   that only does forward translation and only to mark the
   right day in the calendar as "today". */

struct zone {
	char* data;
	size_t len;

	int ttisgmtcnt;
	int ttisstdcnt;
	int leapcnt;
	int timecnt;
	int typecnt;
	int charcnt;

	uint8_t* times;
	uint8_t* ttype;
	uint8_t* types;
	uint8_t* chars;
	uint8_t* leaps;
};

static int mmap_zonefile(struct zone* zf, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		return ret;
	if(st.size < 20 + 6*4)
		return -EINVAL;

	int prot = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	zf->data = buf;
	zf->len = st.size;

	return 0;
}

static int32_t beint32(uint8_t* p)
{
	return (p[0] << 3*8) | (p[1] << 2*8) | (p[2] << 1*8) | p[3];
};

static int prep_zonefile(struct zone* zf)
{
	uint8_t* data = (uint8_t*) zf->data;
	int len = zf->len;

	if(len < 50 || strncmp(zf->data, "TZif", 4))
		return -EINVAL;

	zf->ttisgmtcnt = beint32(data + 20 + 0*4);
	zf->ttisstdcnt = beint32(data + 20 + 1*4);
	zf->leapcnt = beint32(data + 20 + 2*4);
	zf->timecnt = beint32(data + 20 + 3*4);
	zf->typecnt = beint32(data + 20 + 4*4);
	zf->charcnt = beint32(data + 20 + 5*4);

	if(zf->timecnt < 0 || zf->typecnt < 0)
		return -EINVAL;

	int expfilelen = 20 + 6*4
		+ zf->timecnt*4
		+ zf->timecnt*1
		+ (4 + 1 + 1)*zf->typecnt;

	if(expfilelen > len || expfilelen < 0)
		return -EINVAL;

	zf->times = data + 20 + 6*4;
	zf->ttype = zf->times + 4*zf->timecnt;
	zf->types = zf->ttype + zf->timecnt;
	zf->chars = zf->types + 6*zf->typecnt;
	zf->leaps = zf->chars + zf->charcnt;

	return 0;
}

static time_t tz_times(struct zone* tz, int i)
{
	if(i < 0 || i >= tz->timecnt)
		return 0;
	return (time_t) beint32(tz->times + 4*i);
}

static int tz_ttype(struct zone* tz, int i)
{
	if(i < 0 || i >= tz->timecnt)
		return 0;
	return tz->ttype[i];
}

static int tz_types(struct zone* tz, int i)
{
	if(i < 0 || i >= tz->typecnt)
		return 0;
	return beint32(tz->types + 6*i);
}

static int tz_timecnt(struct zone* tz)
{
	return tz->timecnt;
}

static void zone_shift_fwd(struct zone* tz, struct timeval* tv)
{
	time_t ts = tv->sec;
	int i;

	for(i = 0; i < tz_timecnt(tz); i++)
		if(tz_times(tz, i) >= ts)
			break;

	int tt = tz_ttype(tz, i > 0 ? i - 1 : 0);
	int zo = tz_types(tz, tt);

	tv->sec = ts + zo;
}

static void translate_to_local(CTX, struct timeval* tv)
{
	struct zone zf;

	memzero(&zf, sizeof(zf));

	if(mmap_zonefile(&zf, "/etc/localtime") < 0)
		return;
	if(prep_zonefile(&zf) < 0)
		return;

	zone_shift_fwd(&zf, tv);
}

static void query_local_time(CTX)
{
	struct timeval tv;
	struct tm tm;
	int ret;

	if((ret = sys_gettimeofday(&tv, NULL)) < 0)
		fail("gettimeofday", NULL, ret);

	translate_to_local(ctx, &tv);

	tv2tm(&tv, &tm);

	if(!ctx->day && !ctx->month && !ctx->year)
		ctx->day = tm.mday;
	if(!ctx->month && !ctx->year)
		ctx->month = tm.mon + 1;
	if(!ctx->year)
		ctx->year = tm.year + 1900;
}

/* The calendar is composed of 13-week (3-month) blocks rendered
   separately. The initial date is set in ctx, and the block is
   filled by literally counting the days. */

static int is_leap(int year)
{
	if(year % 4)
		return 0;
	if(year % 100)
		return 1;
	if(year % 400)
		return 0;

	return 1;
}

static void fill_column(CTX, short row[7][15], int ci, int rm)
{
	int year = ctx->year;
	int month = ctx->month;
	int day = ctx->day;
	int ri = ctx->weekday - 1;

	while(ri < 7) {
		int mark = day;

		if(day != ctx->mark.day)
			;
		else if(month != ctx->mark.month)
			;
		else mark = -day;

		row[ri++][ci] = mark;

		if(day == 28 && month == 2 && is_leap(year)) {
			day++;
		} else if(day < mthdays[month]) {
			day++;
		} else {
			day = 1;

			if(month < 12) {
				month++;
			} else {
				month = 1;
				year++;
			} if(rm) {
				break;
			}
		}
	}

	ctx->year = year;
	ctx->month = month;
	ctx->day = day;
	ctx->weekday = ri < 7 ? ri + 1 : 1;
}

/* Visual formatting */

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

static void print_header(CTX, int mon)
{
	FMTBUF(p, e, buf, 100);

	p = fmtstr(p, e, "    ");

	for(int i = 0; i < 3; i++) {
		int mi = (mon++ - 1) % 12;
		const char* month = months[mi];
		int mlen = strlen(month);
		int lpad = 4 + 5 - (mlen+1)/2;
		int rpad = 17 - (lpad + mlen);

		if(lpad < 0) lpad = 0;
		if(rpad < 0) rpad = 0;

		p = fmtpadr(p, e, lpad, p);
		p = fmtstr(p, e, "\033[1;37m");
		p = fmtstr(p, e, month);
		p = fmtstr(p, e, "\033[0m");
		p = fmtpadr(p, e, rpad, p);
	}

	p = fmtstr(p, e, "\n");

	FMTENL(p, e);
	output(ctx, buf, p - buf);
}

static char* fmt_lastday(char* p, char* e, int d, int md)
{
	int mark = 0;

	if(!md)
		return fmtstr(p, e, "  ");

	if(md < 0) {
		mark = 1;
		md = -md;
		p = fmtstr(p, e, "\033[30;43m");
	} else if(d == 5 || d == 6) { /* sat or sun */
		mark = 1;
		p = fmtstr(p, e, "\033[31m");
	}

	if(md < 10)
		p = fmtstr(p, e, " ");
	p = fmtint(p, e, md);

	if(mark)
		p = fmtstr(p, e, "\033[0m");

	return p;
}

static char* fmt_cellday(char* p, char* e, int d, int md)
{
	p = fmtstr(p, e, "  ");
	p = fmt_lastday(p, e, d, md);

	return p;
}

static char* fmt_leftday(char* p, char* e, int d, int md)
{
	if(md >= 1 && md <= 9) {
		p = fmt_lastday(p, e, d, md);
	} else {
		p = fmtstr(p, e, "  ");
	}

	return p;
}

static char* fmt_sidetag(char* p, char* e, int d)
{
	p = fmtstr(p, e, "\033[33m");
	p = fmtstr(p, e, weekdays[d]);
	p = fmtstr(p, e, "\033[0m");

	return p;
}

static void print_cline(CTX, short row[7][15], int d)
{
	int md, c = 0;

	FMTBUF(p, e, buf, 256);

	if((md = row[d][c]) == TAG)
		p = fmt_sidetag(p, e, d);
	else
		p = fmt_leftday(p, e, d, md);

	for(c = 1; c <= 13; c++)
		p = fmt_cellday(p, e, d, row[d][c]);


	if((md = row[d][c]) == TAG) {
		p = fmtstr(p, e, "   ");
		p = fmt_sidetag(p, e, d);
	} else {
		p = fmtstr(p, e, "  ");
		p = fmt_lastday(p, e, d, md);
	}

	FMTENL(p, e);
	output(ctx, buf, p - buf);
}

static void empty_line(CTX)
{
	output(ctx, "\n", 1);
}

static void calendar_row(CTX, int lm, int rm)
{
	short row[7][15];
	const int L = 0;
	const int R = 1+13+1 - 1;
	int c, d;

	memzero(row, 7*15*sizeof(short));

	if(ctx->weekday >= 5 && lm)
		fill_column(ctx, row, 0, 0);

	for(c = 1; c <= 13; c++)
		fill_column(ctx, row, c, 0);

	if(ctx->day >= 27 && rm)
		fill_column(ctx, row, c, 1);

	for(d = 0; d < 7; d++)
		if(!row[d][L] && (d >= 6 || !row[d+1][L]))
			row[d][L] = TAG;
	for(d = 6; d >= 0; d--)
		if(!row[d][R] && (d <= 0 || !row[d-1][R]))
			row[d][R] = TAG;

	for(d = 0; d < 7; d++)
		print_cline(ctx, row, d);

}

/* Initially year/month/day in ctx is set to the current date
   (or whatever date was specified). Here we save the day that
   should be marked, and rewind the ctx date back to the start
   of the block being shown first, which should be at least a
   month before the marked date.

   The remaining part of the function determines day of the week
   for the initial date using the doomsday rule and counting
   either back or fore from March 0. */

static void start_from(CTX, int year, int month)
{
	int day = 1;

	ctx->mark.day = ctx->day;
	ctx->mark.month = ctx->month;

	ctx->year = year;
	ctx->month = month;
	ctx->day = day;

	int c = year / 100;
	int A = 5*(c % 4) % 7 + 2;
	int T = year % 100;

	if(T % 2) { T += 11; }; T = T/2;
	if(T % 2) { T += 11; }; T = 7 - (T % 7);

	int doomsday = (A + T) % 7;
	int weekday, shift = day;

	if(month < 3) {
		int janfeb = 31 + 28 + is_leap(year);

		if(month == 2)
			shift += 31;

		shift = janfeb - shift;
		shift = 7 - (shift % 7);
	} else {
		for(int i = 3; i < month; i++)
			shift += mthdays[i];
	}

	weekday = (doomsday + shift) % 7;
	ctx->weekday = weekday ? weekday : 7;
}

static void show_year(CTX)
{
	start_from(ctx, ctx->year, 1);

	print_header(ctx, 1);
	calendar_row(ctx, 1, 0);
	empty_line(ctx);

	print_header(ctx, 4);
	calendar_row(ctx, 0, 0);
	empty_line(ctx);

	print_header(ctx, 7);
	calendar_row(ctx, 0, 0);
	empty_line(ctx);

	print_header(ctx, 10);
	calendar_row(ctx, 0, 1);
	empty_line(ctx);
}

static void show_3mon(CTX)
{
	if(ctx->month == 1)
		start_from(ctx, ctx->year - 1, 12);
	else
		start_from(ctx, ctx->year, ctx->month - 1);

	print_header(ctx, ctx->month);
	calendar_row(ctx, 1, 1);
	empty_line(ctx);
}

/* Options parsing. The users are expected to specify year,
   month-year, or month-day-year in any order as long as the
   spec is not ambiguous. */

static void invert_day_mon(CTX)
{
	int day = ctx->day;
	int month = ctx->month;

	if(ctx->opts & SET_mon)
		return;
	if(!ctx->month || !ctx->day)
		return;
	if(ctx->day > 12)
		return;

	ctx->day = month;
	ctx->month = day;
}

static void set_numeric(CTX, char* arg, int val)
{
	if(!ctx->year && (ctx->opts & OPT_y)) {
		ctx->year = val;
	} else if(val > 1000) {
		if(!ctx->year) {
			invert_day_mon(ctx);
			ctx->year = val;
			return;
		}
	} else if(val <= 31 && val > 12) {
		if(!ctx->day) {
			ctx->day = val;
			return;
		}
	} else if(val <= 12) {
		if(!ctx->month) {
			ctx->month = val;
			return;
		} else if(!ctx->day) {
			ctx->day = val;
			return;
		}
	}
	fail("invalid date component", arg, 0);
}

static void set_month(CTX, char* arg, int val)
{
	if(!ctx->month) {
		ctx->month = val;
		ctx->opts |= SET_mon;
	} else if(!ctx->day && !(ctx->opts & SET_mon)) {
		ctx->day = ctx->month;
		ctx->month = val;
	} else {
		fail("invalid date component", arg, 0);
	}
}

static void set_iso_date(CTX, char* arg, int year, char* p)
{
	if(ctx->year || ctx->month || ctx->day)
		fail("misplaced iso date", NULL, 0);

	ctx->year = year;

	if(!(p = parseint(p, &ctx->month)))
		goto inval;
	if(!*p)
		return;
	else if(*p++ != '-')
		goto inval;

	if(!(p = parseint(p, &ctx->day)) || *p)
		goto inval;

	if(ctx->month <= 12 && ctx->day <= mthdays[ctx->month-1])
		return;
inval:
	fail("invalid date", arg, 0);
}

static int parse_month_name(char* arg)
{
	int m, i;

	for(m = 0; m < 12; m++) {
		for(i = 0; i < 3; i++) {
			if(arg[i] == ucmonth[m][i])
				continue;
			if(arg[i] == lcmonth[m][i])
				continue;
			break;
		} if(i < 3) {
			continue;
		} else if(arg[i]) {
			return 0;
		} else {
			return m + 1;
		}
	}

	return 0;
}

static void set_arg(CTX, char* arg)
{
	char* p;
	int val;

	if((p = parseint(arg, &val)) && !*p)
		return set_numeric(ctx, arg, val);
	if(p && *p == '-')
		return set_iso_date(ctx, arg, val, p + 1);
	if((val = parse_month_name(arg)))
		return set_month(ctx, arg, val);

	fail("cannot parse", arg, 0);
}

static void prep_supplied_date(CTX)
{
	int day = ctx->day;
	int month = ctx->month;
	int year = ctx->year;

	if(day && !month)
		fail("invalid date spec", NULL, 0);

	if(day && month && !year) {
		if(!(ctx->opts & SET_mon)) {
			ctx->month = day;
			ctx->day = month;
		}
	}

	if(year && year <= 1582)
		fail("Julian calendar is not supported", NULL, 0);

	if(year && !month)
		ctx->opts |= OPT_y;

	if(!month || !year)
		query_local_time(ctx);
}

int main(int argc, char** argv)
{
	int i = 1;
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = outbuf;
	ctx->bo.len = sizeof(outbuf);

	if(i < argc)
		set_arg(ctx, argv[i++]);
	if(i < argc)
		set_arg(ctx, argv[i++]);
	if(i < argc)
		set_arg(ctx, argv[i++]);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	prep_supplied_date(ctx);

	if(ctx->opts & OPT_y)
		show_year(ctx);
	else
		show_3mon(ctx);

	bufoutflush(&ctx->bo);

	return 0;
}
