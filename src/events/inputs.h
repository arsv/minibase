struct ev {
	int type;
	int size;
	const char* tag;
	const char** names;
	int count;
};

typedef void (*dumper)(char* path, int fd);

extern const struct ev ev_key;
extern const struct ev ev_sw;
extern const struct ev ev_led;
extern const struct ev ev_rel;
extern const struct ev ev_abs;

void forall_inputs(const char* dir, dumper f);
void query_event_bits(int fd);
void with_entry(const char* dir, char* name, dumper f);
void read_events(char* path, int fd);
char* getname(const struct ev* ex, int k);
