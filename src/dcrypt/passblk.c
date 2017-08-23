#include <sys/file.h>

#include <printf.h>
#include <format.h>
#include <string.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "passblk.h"

ERRTAG = "passblk";
ERRLIST = {
	REPORT(EINVAL), REPORT(ENOENT), REPORT(ENOTTY), REPORT(EFAULT),
	REPORT(ENODEV), RESTASNUMBERS
};

void quit(const char* msg, char* arg, int err)
{
	term_fini();
	fail(msg, arg, err);
}

static void ask_pass_setup_dm(void)
{
	char pass[100];
	int plen = sizeof(pass);
again:
	if((plen = input("Passphrase", pass, sizeof(pass))) < 0)
		goto again;

	status("Hashing passphrase");

	if(try_passphrase(pass, plen)) {
		message("Invalid passphrase", 500);
		goto again;
	}

	clearbox();
	term_fini();
	setup_devices();

	if(check_partitions()) {
		term_back();
		message("Bad passphrase or corrupt storage", 1000);
		goto again;
	}
}

int main(int argc, char** argv)
{
	if(argc > 1)
		fail("no arguments allowed", NULL, 0);

	load_config();

	if(any_encrypted_parts())
		open_dm_control();

	open_udev();
	term_init();

	status("Scanning available block devices");
	scan_devs();

	if(any_missing_devs()) {
		status("Waiting for devices");
		wait_udev();
	}

	query_part_inodes();

	status("Linking unencrypted partitions");
	link_plain_partitions();

	if(any_encrypted_parts()) {
		status("Setting up encrypted partitions");
		ask_pass_setup_dm();
	}

	term_fini();

	return 0;
}
