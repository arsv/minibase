struct keyfile {
	int len;
	union {
		char buf[2048];
		struct {
			uint8_t salt[8];
			uint8_t wrapped[];
		};
		struct {
			uint8_t _alt[8];
			uint8_t iv[8];
			uint8_t key[][16];
		};
	};
	uint8_t kek[16];
} keyfile;

int ask(char* tag, char* buf, int len);
void read_keyfile(struct keyfile* kf, char* name);
void unwrap_keyfile(struct keyfile* kf, char* phrase, int phrlen);
void copy_valid_iv(struct keyfile* kf);
void hash_passphrase(struct keyfile* kf, char* phrase, int phrlen);
void write_keyfile(struct keyfile* kf, char* name, int flags);
