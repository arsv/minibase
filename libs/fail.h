extern const struct errcode {
	short code;
	char* name;
} errlist[];

#define ERRTAG const char errtag[]
#define ERRLIST const struct errcode errlist[]

#define REPORT(e) { e, #e }
#define RESTASNUMBERS { 0 }

void warn(const char* msg, const char* obj, int err);
void fail(const char* msg, const char* obj, int err) __attribute__((noreturn));
