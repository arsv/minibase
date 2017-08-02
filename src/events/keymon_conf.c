#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mmap.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <exit.h>
#include <fail.h>

#include "config.h"
#include "keymon.h"

#define MAX_CONFIG_SIZE 0x10000
#define PAGE 4096

struct mbuf {
	void* buf;
	long len;
	long full;
};

struct lbuf {
	char* file;
	int line;
};

struct action actions[NACTIONS];
int nactions;

static void error(struct lbuf* lb, char* msg, char* arg)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, lb->file);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, lb->line);
	p = fmtstr(p, e, ": ");

	if(msg)
		p = fmtstr(p, e, msg);
	if(arg && msg)
		p = fmtstr(p, e, " ");
	if(arg)
		p = fmtstr(p, e, arg);

	*p++ = '\n';

	writeall(STDERR, buf, p - buf);
	_exit(-1);
}

static void read_whole(struct mbuf* mb, char* name)
{
	int fd;
	long ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail("cannot open", name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("cannot stat", name, ret);
	if(st.size > MAX_CONFIG_SIZE)
		fail("file too large:", name, ret);

	int len = st.size;
	int need = len + 1;
	int full = need + (PAGE - need % PAGE) % PAGE;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	ret = sys_mmap(NULL, full, prot, flags, fd, 0);

	if(mmap_error(ret))
		fail("cannot mmap", name, ret);

	void* buf = (void*) ret;

	if((ret = sys_read(fd, buf, len)) < 0)
		fail("read", name, ret);
	else if(ret < len)
		fail("read", name, 0);

	sys_close(fd);

	mb->buf = buf;
	mb->len = len;
	mb->full = full;
}

static void free_buf(struct mbuf* mb)
{
	sys_munmap(mb->buf, mb->full);
	memzero(mb, sizeof(*mb));
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* skip_to_acts(struct lbuf* lb, char* ls, char* le)
{
	char* p;

	for(p = ls; p < le && *p != ':'; p++)
		;
	if(*p != ':')
		error(lb, "missing separator", NULL);

	*p++ = '\0';

	while(*p && isspace(*p)) p++;

	return p;
}

static char* cut_word(char* p)
{
	while(*p && !isspace(*p))
		p++;
	if(!*p)
		return p;
	else
		*p++ = '\0';
	while(*p &&  isspace(*p))
		p++;

	return p;
}

static void parse_cond(struct lbuf* lb, struct action* ka, char* p)
{
	char* next = cut_word(p);
	int code;

	if(!strcmp(p, "hold")) {
		ka->mode |= MODE_HOLD;
		p = next;
	} else if(!strcmp(p, "long")) {
		ka->mode |= MODE_HOLD | MODE_LONG;
		p = next;
	}

	if(!strncmp(p, "C-", 2)) { p += 2; ka->mode |= MODE_CTRL; }
	if(!strncmp(p, "A-", 2)) { p += 2; ka->mode |= MODE_ALT;  }

	if((code = find_key(p)))
		ka->code = code;
	else
		error(lb, "unknown key", p);
}

static void parse_acts(struct lbuf* lb, struct action* ka, char* p)
{
	char* cmd = p;
	char* arg = cut_word(p);

	int cmdlen = strnlen(cmd, sizeof(ka->cmd));
	int arglen = strnlen(arg, sizeof(ka->arg));

	if(cmdlen >= sizeof(ka->cmd))
		error(lb, "action name too long", NULL);
	if(arglen >= sizeof(ka->arg))
		error(lb, "argument too long", NULL);
	if(!cmdlen)
		error(lb, "empty command", NULL);

	memcpy(ka->cmd, cmd, cmdlen);
	ka->cmd[cmdlen] = '\0';

	memcpy(ka->arg, arg, arglen);
	ka->arg[arglen] = '\0';
}

static void parse_line(struct lbuf* lb, char* ls, char* le)
{
	char* cond = ls;
	char* acts = skip_to_acts(lb, ls, le);

	if(nactions >= NACTIONS)
		error(lb, "out of action slots", NULL);

	struct action* ka = &actions[nactions++];

	parse_cond(lb, ka, cond);
	parse_acts(lb, ka, acts);
}

void load_config(void)
{
	struct mbuf mb;
	struct lbuf lb = { CONFIG, 0 };

	read_whole(&mb, CONFIG);

	char* buf = mb.buf;
	char* end = mb.buf + mb.len;
	char *ls, *le;

	for(ls = buf; ls < end; ls = le + 1) {
		lb.line++;
		le = strecbrk(ls, end, '\n');
		*le = '\0';

		while(ls < le && isspace(*ls))
			ls++;
		if(ls >= le)
			continue;
		if(*ls == '#')
			continue;

		parse_line(&lb, ls, le);
	}

	free_buf(&mb);
}
