#include <bits/ints.h>

struct scrypt {
	void* dk; int dklen;
	void* pass; int passlen;
	void* salt; int saltlen;
	void* temp; int templen;
	int n; /* CPU/memory cost parameter */
	int p; /* parallelization parameter */
	int r; /* block size */
};

long scrypt_init(struct scrypt* sc, int n, int r, int p);
int scrypt_temp(struct scrypt* sc, void* buf, long len);
int scrypt_data(struct scrypt* sc, void* P, int plen, void* S, int slen);
void scrypt_hash(struct scrypt* sc, void* dk, int dklen);
