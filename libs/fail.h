extern const struct errcode {
	short code;
	char* name;
} errlist[];

#define ERRLIST const struct errcode errlist[]

#define REPORT(e) { e, #e }
#define RESTASNUMBERS { 0 }

void fail(const char* tag, const char* msg, const char* obj, int err);
