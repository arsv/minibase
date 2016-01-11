#include <sys/write.h>
#include <sys/sysinfo.h>

#include <argbits.h>
#include <fmtstr.h>
#include <fmtlong.h>
#include <fmtint32.h>
#include <fmtulp.h>
#include <fail.h>
#include <null.h>

ERRTAG = "sysinfo";
ERRLIST = {
	REPORT(EFAULT), REPORT(EINVAL), REPORT(ENOSYS),
	RESTASNUMBERS
};

static char* fmtpart(char* p, char* end, long n, char* unit)
{
	if(!n) goto out;
	p = fmtstr(p, end, " ");
	p = fmtlong(p, end, n);
	p = fmtstr(p, end, " ");
	p = fmtstr(p, end, unit);
	if(n <= 1) goto out;
	p = fmtstr(p, end, "s");
out:	return p;
}

static char* fmtuptime(char* p, char* end, long ts)
{
	int sec = ts % 60; ts /= 60;
	int min = ts % 60; ts /= 60;
	int hrs = ts % 24; ts /= 24;

	p = fmtpart(p, end, ts, "day");
	p = fmtpart(p, end, hrs, "hour");
	p = fmtpart(p, end, min, "minute");
	p = fmtpart(p, end, sec, "second");

	return p;
}

static char* fmtavg(char* p, char* end, unsigned long avg, char* over)
{
	int avgperc = (avg >> 16) & 0xFFFF;
	int avgfrac = ((avg & 0xFFFF)*100 >> 16) & 0xFFFF;

	p = fmti32(p, end, avgperc);
	p = fmtstr(p, end, ".");
	p = fmtulp(p, end, avgfrac, 2);
	p = fmtstr(p, end, "% over ");
	p = fmtstr(p, end, over);

	return p;
}

static char* fmtmem(char* p, char* end, unsigned long n, int mu)
{
	unsigned long nb = n * mu;
	unsigned long fr = 0;
	char* suff = "";

	if(nb > 1024) { suff = "K"; fr = nb % 1024; nb /= 1024; }
	if(nb > 1024) { suff = "M"; fr = nb % 1024; nb /= 1024; }
	if(nb > 1024) { suff = "G"; fr = nb % 1024; nb /= 1024; }

	fr /= 102;

	p = fmtu32(p, end, nb);
	if(fr) {
		p = fmtstr(p, end, ".");
		p = fmtulp(p, end, fr, 2);
	}
	p = fmtstr(p, end, suff);

	return p;
}

static char* fmtline1(char* p, char* end, struct sysinfo* si)
{
	p = fmtstr(p, end, "Uptime");
	p = fmtuptime(p, end, si->uptime);	
	p = fmtstr(p, end, ", ");
	p = fmti32(p, end, si->procs);
	p = fmtstr(p, end, si->procs > 1 ? " processes" : "process");
	p = fmtstr(p, end, "\n");
	return p;
}

static char* fmtline2(char* p, char* end, struct sysinfo* si)
{
	p = fmtstr(p, end, "Load average ");
	p = fmtavg(p, end, si->loads[0], "1 min");
	p = fmtstr(p, end, ", ");
	p = fmtavg(p, end, si->loads[1], "5 min");
	p = fmtstr(p, end, ", ");
	p = fmtavg(p, end, si->loads[2], "15 min");
	p = fmtstr(p, end, "\n");
	return p;
}

static char* fmtline3(char* p, char* end, struct sysinfo* si)
{
	p = fmtstr(p, end, "RAM ");
	p = fmtmem(p, end, si->totalram, si->mem_unit);
	p = fmtstr(p, end, " (");
	p = fmtmem(p, end, si->freeram, si->mem_unit);
	p = fmtstr(p, end, " free, ");
	p = fmtmem(p, end, si->sharedram, si->mem_unit);
	p = fmtstr(p, end, " shared, ");
	p = fmtmem(p, end, si->bufferram, si->mem_unit);
	p = fmtstr(p, end, " buffers)");
	p = fmtstr(p, end, "\n");
	return p;
}

static char* fmtline4(char* p, char* end, struct sysinfo* si)
{
	if(!si->totalswap && !si->totalhigh)
		return p;

	if(si->totalswap) {
		p = fmtstr(p, end, "Swap ");
		p = fmtmem(p, end, si->totalswap, si->mem_unit);
		p = fmtstr(p, end, " (");
		p = fmtmem(p, end, si->freeswap, si->mem_unit);
		p = fmtstr(p, end, " free)");
	} if(si->totalhigh) {
		p = fmtstr(p, end, si->totalswap ? " high " : "High");
		p = fmtmem(p, end, si->totalhigh, si->mem_unit);
		p = fmtstr(p, end, " (");
		p = fmtmem(p, end, si->freehigh, si->mem_unit);
		p = fmtstr(p, end, " free)");
	}
	p = fmtstr(p, end, "\n");

	return p;
}

static void showall(struct sysinfo* si)
{
	char buf[1024];
	char* end = buf + sizeof(buf) - 1;
	char* p = buf;

	p = fmtline1(p, end, si);
	p = fmtline2(p, end, si);
	p = fmtline3(p, end, si);
	p = fmtline4(p, end, si);

	syswrite(1, buf, p - buf);
}

int main(int argc, char** argv)
{
	int i = 1;
	struct sysinfo si;

	xchk(syssysinfo(&si), "sysinfo", NULL);

	if(i < argc)
		fail("too many arguments", NULL, 0);

	showall(&si);

	return 0;
}
