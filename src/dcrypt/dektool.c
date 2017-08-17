#include <sys/file.h>
#include <sys/stat.h>
#include <sys/brk.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

#include "keytool.h"

ERRTAG = "dektool";
ERRLIST = {
	REPORT(ENOENT), REPORT(ENOTDIR), REPORT(EPERM), REPORT(EACCES),
	REPORT(ELOOP), REPORT(ENOMEM), REPORT(EFAULT), REPORT(EEXIST),
	RESTASNUMBERS
};

#define OPTS "actp"
#define OPT_a (1<<0)
#define OPT_c (1<<1)
#define OPT_t (1<<2)
#define OPT_p (1<<3)

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;
} top;

struct keyfile keyfile;

static void no_other_options(void)
{
	if(top.argi < top.argc)
		fail("too many arguments", NULL, 0);
	if(top.opts)
		fail("bad options", NULL, 0);
}

static int use_opt(int opt)
{
	int ret = top.opts & opt;
	top.opts &= ~opt;
	return ret;
}

static char* shift_arg(void)
{
	if(top.argi < top.argc)
		return top.argv[top.argi++];
	else
		return NULL;
}

static int shift_uint(void)
{
	char *a, *p;
	int val;

	if(!(a = shift_arg()))
		fail("too few arguments", NULL, 0);
	if(!(p = parseint(a, &val)) || *p)
		fail("integer argument required:", a, 0);
	if(val <= 0)
		fail("positive integer required:", a, 0);

	return val;
}

static int count_args(void)
{
	return top.argc - top.argi;
}

static void init_args(int argc, char** argv)
{
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		top.opts = argbits(OPTS, argv[i++] + 1);
	else
		top.opts = 0;

	top.argi = i;
	top.argc = argc;
	top.argv = argv;
}

static void message(char* msg, char* arg)
{
	int msglen = strlen(msg);
	int arglen = arg ? strlen(arg) : 0;

	char buf[msglen+arglen+5];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, msg);

	if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	};

	*p++ = '\n';

	sys_write(STDOUT, buf, p - buf);
}

static void prep_passphrase(struct keyfile* kf)
{
	int buflen = 80;
	char phrase[buflen];
	char repeat[buflen];

	int phrlen = ask("Passphrase: ", phrase, buflen);
	int replen = ask("    repeat: ", repeat, buflen);

	if(phrlen != replen || strcmp(phrase, repeat))
		fail("phrases do not match", NULL, 0);

	memzero(repeat, sizeof(repeat));

	hash_passphrase(kf, phrase, phrlen);

	memzero(phrase, sizeof(phrase));
}

static void load_keyfile(struct keyfile* kf, char* name)
{
	char phrase[80];
	int phrlen;

	read_keyfile(kf, name);
	phrlen = ask("Passphrase: ", phrase, sizeof(phrase));
	unwrap_keyfile(kf, phrase, phrlen);
}

static void read_random(char* buf, int size)
{
	int fd, rd;
	char* random = "/dev/urandom";

	if((fd = sys_open(random, O_RDONLY)) < 0)
		fail("open", random, fd);
	if((rd = sys_read(fd, buf, size)) < 0)
		fail("read", random, rd);
	else if(rd < size)
		fail("not enough random data", NULL, 0);

	sys_close(fd);
}

static void fill_key_data(struct keyfile* kf, int total)
{
	read_random(kf->buf, total);
	copy_valid_iv(kf);
	kf->len = total;
}

static void append_key_data(struct keyfile* kf, int need)
{
	read_random(kf->buf + kf->len, need);
	kf->len += need;
}

void check_not_exists(char* name)
{
	struct stat st;

	if((sys_stat(name, &st)) >= 0)
		fail(NULL, name, -EEXIST);
}

static void create(void)
{
	struct keyfile* kf = &keyfile;
	char* name = shift_arg();
	int size = 16;
	int count = 1;
	int total = sizeof(kf->salt) + sizeof(kf->iv) + count*size;

	message("Creating new keyfile:", name);

	check_not_exists(name);

	if(count_args())
		count = shift_uint();
	no_other_options();

	if(total > sizeof(kf->buf))
		fail("keyring size too large", NULL, 0);

	fill_key_data(kf, total);
	prep_passphrase(kf);

	write_keyfile(kf, name, O_CREAT | O_EXCL);
}

static void addkey(void)
{
	struct keyfile* kf = &keyfile;
	char* name = shift_arg();
	int size = 16;
	int count = shift_uint();
	no_other_options();

	message("Adding keys to", name);

	load_keyfile(kf, name);
	append_key_data(kf, count*size);
	write_keyfile(kf, name, 0);
}

static void trykey(void)
{
	struct keyfile* kf = &keyfile;
	char* name = shift_arg();
	no_other_options();

	message("Testing passphrase for", name);

	load_keyfile(kf, name);

	message("Success, passphrase is likely correct", NULL);
}

static void repass(void)
{
	struct keyfile* kf = &keyfile;
	char* name = shift_arg();
	no_other_options();

	message("Changing passphrase for", name);
	message("Type current passphrase to unwrap the key", NULL);

	load_keyfile(kf, name);

	message("Key unwrapped, type the new passphrase now", NULL);

	prep_passphrase(kf);
	write_keyfile(kf, name, 0);

	message("Success, passphrase changed", NULL);
}

int main(int argc, char** argv)
{
	init_args(argc, argv);

	if(use_opt(OPT_c))
		create();
	else if(use_opt(OPT_a))
		addkey();
	else if(use_opt(OPT_t))
		trykey();
	else if(use_opt(OPT_p))
		repass();
	else	
		fail("no mode specified", NULL, 0);

	return 0;
}
