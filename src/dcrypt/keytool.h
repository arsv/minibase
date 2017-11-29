#define KEYSIZE 32
#define HDRSIZE 16
#define SALTLEN 8

struct keyfile {
	int len;
	byte kek[16];
	byte buf[1024];
} keyfile;

int ask(char* tag, char* buf, int len);
void read_keyfile(struct keyfile* kf, char* name);
void unwrap_keyfile(struct keyfile* kf, char* phrase, int phrlen);
void copy_valid_iv(struct keyfile* kf);
void hash_passphrase(struct keyfile* kf, char* phrase, int phrlen);
void write_keyfile(struct keyfile* kf, char* name, int flags);
byte* get_key_by_idx(struct keyfile* kf, int idx);
int is_valid_key_idx(struct keyfile* kf, int idx);
