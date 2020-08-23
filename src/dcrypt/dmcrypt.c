#include <bits/ioctl/mapper.h>
#include <bits/ioctl/block.h>
#include <bits/major.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#include "keytool.h"

ERRTAG("dmcrypt");
ERRLIST(NENOENT NENOTDIR NEPERM NEACCES NELOOP NENOMEM NEFAULT NEEXIST);

#define OPTS "df"
#define OPT_d (1<<0)
#define OPT_f (1<<1)

struct device {
	char* name;
	char* base;
	uint64_t rdev;
	uint64_t mdev;
	uint64_t size;
	uint8_t* key;
	int fd;
};

int dmfd;
struct keyfile keyfile;

static int error(const char* msg, const char* arg, int err)
{
	warn(msg, arg, err);
	return err ? err : -EINVAL;
}

static int query_dev_inode(struct device* dev)
{
	int fd, ret;
	struct stat st;

	char* pref = "/dev/";
	char* base = dev->base;
	char path[strlen(pref)+strlen(base)+2];
	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, base);
	*p++ = '\0';

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail("open", path, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", path, ret);
	if((ret = sys_ioctl(fd, BLKGETSIZE64, &(dev->size))))
		fail("ioctl BLKGETSIZE64", path, ret);

	dev->rdev = st.rdev;
	dev->mdev = 0;
	dev->fd = fd;

	return 0;
}

static int dm_open_control(void)
{
	char* control = "/dev/mapper/control";
	int fd, ret;
	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if((fd = sys_open(control, O_RDONLY)) < 0)
		fail("open", control, fd);

	if((ret = sys_ioctl(fd, DM_VERSION, &dmi)) < 0)
		fail("ioctl", "DM_VERSION", ret);

	if(dmi.version[0] != DM_VERSION_MAJOR)
		fail("unsupported dm version", NULL, 0);

	return fd;
}

static void putstr(char* buf, char* src, int len)
{
	memcpy(buf, src, len);
	buf[len] = '\0';
}

static int dm_create(struct device* dev)
{
	char* name = dev->name;
	ulong nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		return error(NULL, name, ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(dmfd, DM_DEV_CREATE, &dmi)) < 0)
		return error("ioctl", "DM_DEV_CREATE", ret);

	dev->mdev = dmi.dev;

	return 0;
}

static int dm_single(char* name, uint64_t size, char* targ, char* opts)
{
	struct dm_ioctl* dmi;
	struct dm_target_spec* dts;
	char* optstring;

	uint nlen = strlen(name);
	uint tlen = strlen(targ);
	uint olen = strlen(opts);
	uint dmilen = sizeof(*dmi);
	uint dtslen = sizeof(*dts);

	uint reqlen = dmilen + dtslen + olen + 1;
	char req[reqlen];

	dmi = (struct dm_ioctl*)(req + 0);
	dts = (struct dm_target_spec*)(req + dmilen);
	optstring = (req + dmilen + dtslen);

	memzero(dmi, sizeof(*dmi));
	dmi->version[0] = DM_VERSION_MAJOR;
	dmi->version[1] = 0;
	dmi->version[2] = 0;
	dmi->flags = 0;
	dmi->data_size = sizeof(req);
	dmi->data_start = dmilen;
	dmi->target_count = 1;

	memzero(dts, sizeof(*dts));
	dts->start = 0;
	dts->length = size / 512;
	dts->status = 0;
	dts->next = 0;

	if(nlen > sizeof(dmi->name) - 1)
		return error(NULL, name, -ENAMETOOLONG);
	if(tlen > sizeof(dts->type) - 1)
		return error(NULL, targ, -ENAMETOOLONG);

	putstr(dmi->name, name, nlen);
	putstr(dts->type, targ, tlen);
	putstr(optstring, opts, olen);

	int ret;

	if((ret = sys_ioctl(dmfd, DM_TABLE_LOAD, req)) < 0)
		warn("ioctl", "DM_TABLE_LOAD", ret);

	return ret;
}

static int dm_crypt(struct device* dev, char* cipher, void* key, int keylen)
{
	int ciphlen = strlen(cipher);

	FMTBUF(p, e, buf, ciphlen + 2*keylen + 50);
	p = fmtstr(p, e, cipher);
	p = fmtstr(p, e, " ");
	p = fmtbytes(p, e, key, keylen);
	p = fmtstr(p, e, " 0 ");
	p = fmtlong(p, e, major(dev->rdev));
	p = fmtstr(p, e, ":");
	p = fmtlong(p, e, minor(dev->rdev));
	p = fmtstr(p, e, " 0 1 allow_discards");
	FMTEND(p, e);

	return dm_single(dev->name, dev->size, "crypt", buf);
}

static int dm_suspend(char* name, int flags)
{
	unsigned nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = flags,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		return error(NULL, name, -ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(dmfd, DM_DEV_SUSPEND, &dmi)) < 0)
		warn("ioctl", "DM_DEV_SUSPEND", ret);

	return ret;
}

static void dm_remove(char* name)
{
	uint nlen = strlen(name);
	int ret;

	struct dm_ioctl dmi = {
		.version = { DM_VERSION_MAJOR, 0, 0 },
		.flags = 0,
		.data_size = sizeof(dmi)
	};

	if(nlen > sizeof(dmi.name) - 1)
		fail(NULL, name, ENAMETOOLONG);

	putstr(dmi.name, name, nlen);

	if((ret = sys_ioctl(dmfd, DM_DEV_REMOVE, &dmi)) < 0)
		fail("ioctl", "DM_DEV_REMOVE", ret);
}

static int create_dm_crypt(struct device* dev)
{
	char* name = dev->name;
	int ret;

	char* cipher = "aes-xts-plain64";

	dm_create(dev);

	if((ret = dm_crypt(dev, cipher, dev->key, KEYSIZE)))
		goto out;
	if((ret = dm_suspend(dev->name, 0)))
		goto out;
out:
	if(ret < 0)
		dm_remove(name);

	return ret;
}

static void remove_dm_crypt(char* name, int opts)
{
	if(opts & OPT_f) {
		dm_single(name, 512, "error", "");
		dm_suspend(name, 0);
	}

	dm_remove(name);
}

static void remove_devices(int n, char** args, int opts)
{
	for(int i = 0; i < n; i++)
		remove_dm_crypt(args[i], opts);
}

static void prepare_device(struct device* dev, char* arg, int* ki)
{
	int k;
	char *p, *base, *name;

	if((p = parseint(arg, &k)) && *p == ':') {
		*ki = k;
		arg = p + 1;
	}

	name = arg;

	if(*(p = strcbrk(arg, ':')) == ':') {
		*p = '\0';
		base = p + 1;
	} else {
		base = name;
	};

	int kidx = *ki;

	if(!is_valid_key_idx(&keyfile, kidx))
		fail("invalid key index for", dev->name, 0);

	dev->base = base;
	dev->name = name;
	dev->key = get_key_by_idx(&keyfile, kidx);

	query_dev_inode(dev);
}

static void setup_devices(int n, char** reqs, int opts)
{
	struct device devs[n];
	int ret = 0, i, kidx = 1;

	memzero(devs, sizeof(devs));

	for(i = 0; i < n; i++)
		prepare_device(&devs[i], reqs[i], &kidx);

	for(i = 0; i < n; i++)
		if((ret = create_dm_crypt(&devs[i])) < 0)
			break;
	if(!ret)
		return;

	for(i--; i >= 0; i--)
		remove_dm_crypt(devs[i].name, opts);
}

static void load_keyfile(char* name)
{
	struct keyfile* kf = &keyfile;
	char phrase[80];
	int phrlen;

	read_keyfile(kf, name);
	phrlen = ask("Passphrase: ", phrase, sizeof(phrase));
	unwrap_keyfile(kf, phrase, phrlen);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	dmfd = dm_open_control();

	if(opts & OPT_d) {
		remove_devices(argc - i, argv + i, opts);
	} else {
		load_keyfile(argv[i++]);
		setup_devices(argc - i, argv + i, opts);
	}

	return 0;
}
