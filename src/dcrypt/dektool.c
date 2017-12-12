#include <sys/file.h>
#include <sys/mman.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "keytool.h"

ERRTAG("dektool");

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;
	struct keyfile* kf;
};

#define CTX struct top* ctx

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
}

static char* shift_arg(CTX)
{
	if(ctx->argi < ctx->argc)
		return ctx->argv[ctx->argi++];
	else
		return NULL;
}

static int shift_uint(CTX)
{
	char *a, *p;
	int val;

	if(!(a = shift_arg(ctx)))
		fail("too few arguments", NULL, 0);
	if(!(p = parseint(a, &val)) || *p)
		fail("integer argument required:", a, 0);
	if(val <= 0)
		fail("positive integer required:", a, 0);

	return val;
}

static int count_args(CTX)
{
	return ctx->argc - ctx->argi;
}

static void init_args(CTX, int argc, char** argv)
{
	ctx->argi = 1;
	ctx->argc = argc;
	ctx->argv = argv;
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

static void prep_passphrase(CTX)
{
	int buflen = 80;
	char phrase[buflen];
	char repeat[buflen];

	int phrlen = ask("Passphrase: ", phrase, buflen);
	int replen = ask("    repeat: ", repeat, buflen);

	if(phrlen != replen || strcmp(phrase, repeat))
		fail("phrases do not match", NULL, 0);

	memzero(repeat, sizeof(repeat));

	hash_passphrase(ctx->kf, phrase, phrlen);

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

static void read_random(void* buf, int size)
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
	if(kf->len + need > sizeof(kf->buf))
		fail("not enough space in keyfile", NULL, 0);

	read_random(kf->buf + kf->len, need);

	kf->len += need;
}

static void check_not_exists(char* name)
{
	struct stat st;

	if((sys_stat(name, &st)) >= 0)
		fail(NULL, name, -EEXIST);
}

static void create_keyfile(CTX, char* name, int count, int mode)
{
	struct keyfile* kf = ctx->kf;
	ulong total = HDRSIZE + count*KEYSIZE;

	if(total > sizeof(kf->buf))
		fail("keyring size too large", NULL, 0);

	message("Creating new keyfile:", name);

	fill_key_data(kf, total);
	prep_passphrase(ctx);

	write_keyfile(kf, name, O_CREAT | mode);
}

static void cmd_create(CTX)
{
	char* name = shift_arg(ctx);
	int count = count_args(ctx) ? shift_uint(ctx) : 1;
	no_other_options(ctx);

	check_not_exists(name);
	create_keyfile(ctx, name, count, O_EXCL);
}

static void cmd_crover(CTX)
{
	char* name = shift_arg(ctx);
	int count = count_args(ctx) ? shift_uint(ctx) : 1;
	no_other_options(ctx);

	create_keyfile(ctx, name, count, O_TRUNC);
}

static void cmd_addkey(CTX)
{
	struct keyfile* kf = ctx->kf;
	char* name = shift_arg(ctx);
	int count = shift_uint(ctx);
	no_other_options(ctx);

	message("Adding keys to", name);

	load_keyfile(kf, name);
	append_key_data(kf, count*KEYSIZE);
	write_keyfile(kf, name, 0);
}

static void cmd_pcheck(CTX)
{
	struct keyfile* kf = ctx->kf;
	char* name = shift_arg(ctx);
	no_other_options(ctx);

	message("Testing passphrase for", name);

	load_keyfile(kf, name);

	message("Success, passphrase is likely correct", NULL);
}

static void cmd_dump(CTX)
{
	struct keyfile* kf = ctx->kf;
	char* name = shift_arg(ctx);
	int kidx = 0, count;

	if(count_args(ctx))
		kidx = shift_uint(ctx);

	no_other_options(ctx);

	load_keyfile(kf, name);

	if(kidx)
		count = 1;
	else
		count = (kf->len - HDRSIZE) / KEYSIZE;

	FMTBUF(p, e, out, count*(2*KEYSIZE+1));

	if(kidx) {
		p = fmtbytes(p, e, get_key_by_idx(kf, kidx), KEYSIZE);
		p = fmtstr(p, e, "\n");
	} else for(int i = 1; i <= count; i++) {
		p = fmtbytes(p, e, get_key_by_idx(kf, i), KEYSIZE);
		p = fmtstr(p, e, "\n");
	};

	FMTEND(p, e);

	writeall(STDOUT, out, p - out);


}

static void cmd_repass(CTX)
{
	struct keyfile* kf = ctx->kf;
	char* name = shift_arg(ctx);
	no_other_options(ctx);

	message("Changing passphrase for", name);
	message("Type current passphrase to unwrap the key", NULL);

	load_keyfile(kf, name);

	message("Key unwrapped, type the new passphrase now", NULL);

	prep_passphrase(ctx);
	write_keyfile(kf, name, 0);

	message("Success, passphrase changed", NULL);
}

static const struct cmd {
	char name[8];
	void (*call)(CTX);
} commands[] = {
	{ "create", cmd_create },
	{ "crover", cmd_crover },
	{ "add",    cmd_addkey },
	{ "test",   cmd_pcheck },
	{ "dump",   cmd_dump   },
	{ "repass", cmd_repass }
};

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	struct keyfile keyfile;
	char* cmd;
	const struct cmd* cc;

	memzero(ctx, sizeof(*ctx));

	init_args(ctx, argc, argv);
	ctx->kf = &keyfile;

	if(!(cmd = shift_arg(ctx)))
		fail("no command specified", NULL, 0);

	for(cc = commands; cc < ARRAY_END(commands); cc++) {
		if(!strcmp(cc->name, cmd)) {
			cc->call(ctx);
			return 0;
		}
	}

	fail("unknown command", cmd, 0);
}
