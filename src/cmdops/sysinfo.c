#include <sys/file.h>
#include <sys/info.h>

#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("sysinfo");
ERRLIST(NEFAULT NEINVAL NENOSYS);

static char* fmtpart(char* p, char* e, long n, char* unit)
{
	if(!n) goto out;
	p = fmtstr(p, e, " ");
	p = fmtlong(p, e, n);
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, unit);
	if(n <= 1) goto out;
	p = fmtstr(p, e, "s");
out:	return p;
}

static char* fmtuptime(char* p, char* e, long ts)
{
	int sec = ts % 60; ts /= 60;
	int min = ts % 60; ts /= 60;
	int hrs = ts % 24; ts /= 24;

	p = fmtpart(p, e, ts, "day");
	p = fmtpart(p, e, hrs, "hour");
	p = fmtpart(p, e, min, "minute");
	p = fmtpart(p, e, sec, "second");

	return p;
}

static char* fmtavg(char* p, char* e, unsigned long avg, char* over)
{
	int avgperc = (avg >> 16) & 0xFFFF;
	int avgfrac = ((avg & 0xFFFF)*100 >> 16) & 0xFFFF;

	p = fmti32(p, e, avgperc);
	p = fmtstr(p, e, ".");
	p = fmtulp(p, e, avgfrac, 2);
	p = fmtstr(p, e, "% over ");
	p = fmtstr(p, e, over);

	return p;
}

static char* fmtmem(char* p, char* e, unsigned long n, int mu)
{
	unsigned long nb = n * mu;
	unsigned long fr = 0;
	char* suff = "";

	if(nb > 1024) { suff = "K"; fr = nb % 1024; nb /= 1024; }
	if(nb > 1024) { suff = "M"; fr = nb % 1024; nb /= 1024; }
	if(nb > 1024) { suff = "G"; fr = nb % 1024; nb /= 1024; }

	fr /= 102;

	p = fmtu32(p, e, nb);
	if(fr) {
		p = fmtstr(p, e, ".");
		p = fmtulp(p, e, fr, 2);
	}
	p = fmtstr(p, e, suff);

	return p;
}

static char* fmtline1(char* p, char* e, struct sysinfo* si)
{
	p = fmtstr(p, e, "Uptime");
	p = fmtuptime(p, e, si->uptime);
	p = fmtstr(p, e, ", ");
	p = fmti32(p, e, si->procs);
	p = fmtstr(p, e, si->procs > 1 ? " processes" : "process");
	p = fmtstr(p, e, "\n");
	return p;
}

static char* fmtline2(char* p, char* e, struct sysinfo* si)
{
	p = fmtstr(p, e, "Load average ");
	p = fmtavg(p, e, si->loads[0], "1 min");
	p = fmtstr(p, e, ", ");
	p = fmtavg(p, e, si->loads[1], "5 min");
	p = fmtstr(p, e, ", ");
	p = fmtavg(p, e, si->loads[2], "15 min");
	p = fmtstr(p, e, "\n");
	return p;
}

static char* fmtline3(char* p, char* e, struct sysinfo* si)
{
	p = fmtstr(p, e, "RAM ");
	p = fmtmem(p, e, si->totalram, si->mem_unit);
	p = fmtstr(p, e, " (");
	p = fmtmem(p, e, si->freeram, si->mem_unit);
	p = fmtstr(p, e, " free, ");
	p = fmtmem(p, e, si->sharedram, si->mem_unit);
	p = fmtstr(p, e, " shared, ");
	p = fmtmem(p, e, si->bufferram, si->mem_unit);
	p = fmtstr(p, e, " buffers)");
	p = fmtstr(p, e, "\n");
	return p;
}

static char* fmtline4(char* p, char* e, struct sysinfo* si)
{
	if(!si->totalswap && !si->totalhigh)
		return p;

	if(si->totalswap) {
		p = fmtstr(p, e, "Swap ");
		p = fmtmem(p, e, si->totalswap, si->mem_unit);
		p = fmtstr(p, e, " (");
		p = fmtmem(p, e, si->freeswap, si->mem_unit);
		p = fmtstr(p, e, " free)");
	} if(si->totalhigh) {
		p = fmtstr(p, e, si->totalswap ? " high " : "High");
		p = fmtmem(p, e, si->totalhigh, si->mem_unit);
		p = fmtstr(p, e, " (");
		p = fmtmem(p, e, si->freehigh, si->mem_unit);
		p = fmtstr(p, e, " free)");
	}
	p = fmtstr(p, e, "\n");

	return p;
}

static void showall(struct sysinfo* si)
{
	char buf[1024];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtline1(p, e, si);
	p = fmtline2(p, e, si);
	p = fmtline3(p, e, si);
	p = fmtline4(p, e, si);

	sys_write(1, buf, p - buf);
}

int main(int argc, char** argv)
{
	struct sysinfo si;
	int ret;

	(void)argv;

	if(argc > 1)
		fail("too many arguments", NULL, 0);
	if((ret = sys_info(&si)) < 0)
		fail("sysinfo", NULL, ret);

	showall(&si);

	return 0;
}
