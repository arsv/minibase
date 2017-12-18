#define bits(name,n) ulong name[(n+sizeof(long)-1)/sizeof(long)]

struct info {
	int iid;
	int eid;

	char name[34];

	bits(ev,   32);
	bits(key, 256);
	bits(rel,  16);
	bits(abs,  64);
	bits(led,  10);
	bits(sw,   16);
};

#undef bits

struct ev {
	const char* tag;
	const char** names;
	int count;
};

#define EVNAMES(name, base) \
	const struct ev ev_##base = { \
		.tag = name, \
		.names = base, \
		.count = ARRAY_SIZE(base) \
	}

extern const struct ev ev_key;
extern const struct ev ev_sw;
extern const struct ev ev_led;
extern const struct ev ev_rel;
extern const struct ev ev_abs;
