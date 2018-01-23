#include <cdefs.h>
#include <format.h>
#include <endian.h>
#include <util.h>

static char* fl(char* p, char* e, char* file, int line)
{
	p = fmtstr(p, e, file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, line);
	p = fmtstr(p, e, ":");

	return p;
}

static int check32(char* file, int line, uint32_t exp, uint32_t got)
{
	int ret = !!(got == exp);

	FMTBUF(p, e, buf, 100);
	p = fl(p, e, file, line);

	if(ret) {
		p = fmtstr(p, e, " OK 0x");
		p = fmtx32(p, e, got);
	} else {
		p = fmtstr(p, e, " FAIL 0x");
		p = fmtx32(p, e, got);
		p = fmtstr(p, e, " expected 0x");
		p = fmtx32(p, e, exp);
	}

	FMTENL(p, e);
	writeall(STDERR, buf, p - buf);

	return !ret;
}

static int check64(char* file, int line, uint64_t exp, uint64_t got)
{
	int ret = !!(got == exp);

	FMTBUF(p, e, buf, 100);
	p = fl(p, e, file, line);

	if(ret) {
		p = fmtstr(p, e, " OK 0x");
		p = fmtx64(p, e, got);
	} else {
		p = fmtstr(p, e, " FAIL 0x");
		p = fmtx64(p, e, got);
		p = fmtstr(p, e, " expected 0x");
		p = fmtx64(p, e, exp);
	}

	FMTENL(p, e);
	writeall(STDERR, buf, p - buf);

	return !ret;
}

#define FL __FILE__, __LINE__

#define BE16 0xA1B2
#define LE16 0xB2A1

#define BE32 0xA1B2C3D4
#define LE32 0xD4C3B2A1

#if BITS == 64
#define BE64 0xA1B2C3D4E5F60718UL
#define LE64 0x1807F6E5D4C3B2A1UL
#else
#define BE64 0xA1B2C3D4E5F60718ULL
#define LE64 0x1807F6E5D4C3B2A1ULL
#endif

int main(void)
{
	int ret = 0;
	byte buf[] = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18 };

	uint64_t v64 = *((uint64_t*)buf);
	uint32_t v32 = *((uint32_t*)buf);
	uint16_t v16 = *((uint16_t*)buf);

	ret |= check32(FL, BE32, ntohl(v32));
#ifdef BIGENDIAN
	ret |= check32(FL, BE32, v32);
	ret |= check32(FL, LE32, swabl(v32));
#else
	ret |= check32(FL, LE32, v32);
	ret |= check32(FL, BE32, swabl(v32));
#endif

	ret |= check32(FL, BE16, ntohs(v16));
#ifdef BIGENDIAN
	ret |= check32(FL, BE16, v16);
	ret |= check32(FL, LE16, swabs(v16));
#else
	ret |= check32(FL, LE16, v16);
	ret |= check32(FL, BE16, swabs(v16));
#endif

#ifdef BIGENDIAN
	ret |= check64(FL, BE64, v64);
	ret |= check64(FL, LE64, swabx(v64));
#else
	ret |= check64(FL, LE64, v64);
	ret |= check64(FL, BE64, swabx(v64));
#endif

	return ret;
}
