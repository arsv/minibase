struct mbuf {
	char* buf;
	long len;
	long full;
};

void modprobe(char* name, char* pars, char** envp);
void insmod(char* path, char* pars, char** envp);

void mmapwhole(struct mbuf* mb, char* name);
void decompress(struct mbuf* mb, char* path, char* cmd, char** envp);
void munmapbuf(struct mbuf* mb);
