#include <bits/types.h>

struct scrypt {
	void* dk; uint dklen;
	void* pass; uint passlen;
	void* salt; uint saltlen;
	void* temp; uint templen;
	uint n; /* CPU/memory cost parameter */
	uint p; /* parallelization parameter */
	uint r; /* block size */
};

ulong scrypt_init(struct scrypt* sc, uint n, uint r, uint p);
int scrypt_temp(struct scrypt* sc, void* buf, ulong len);
int scrypt_data(struct scrypt* sc, void* P, uint plen, void* S, uint slen);
void scrypt_hash(struct scrypt* sc, void* dk, uint dklen);
