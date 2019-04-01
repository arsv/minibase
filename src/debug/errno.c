#include <errnames.h>
#include <sys/file.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#define TAG "errno"

/* I'm going to hell for this but hey, the tool is now like half
   the size of what it was with a nice clean struct here.
   Those 8-byte pointers, ugh. And 4-byte errnos. */

static const char errors[] =
	NEPERM "Operation not permitted\0"
	NENOENT "No such file or directory\0"
	NESRCH "No such process\0"
	NEINTR "Interrupted system call\0"
	NEIO "I/O error\0"
	NENXIO "No such device or address\0"
	NE2BIG "Arg list too long\0"
	NENOEXEC "Exec format error\0"
	NEBADF "Bad file number\0"
	NECHILD "No child processes\0"
	NEAGAIN "Try again\0"
	NENOMEM "Out of memory\0"
	NEACCES "Permission denied\0"
	NEFAULT "Bad address\0"
	NENOTBLK "Block device required\0"
	NEBUSY "Device or resource busy\0"
	NEEXIST "File exists\0"
	NEXDEV "Cross-device link\0"
	NENODEV "No such device\0"
	NENOTDIR "Not a directory\0"
	NEISDIR "Is a directory\0"
	NEINVAL "Invalid argument\0"
	NENFILE "File table overflow\0"
	NEMFILE "Too many open files\0"
	NENOTTY "Not a typewriter\0"
	NETXTBSY "Text file busy\0"
	NEFBIG "File too large\0"
	NENOSPC "No space left on device\0"
	NESPIPE "Illegal seek\0"
	NEROFS "Read-only file system\0"
	NEMLINK "Too many links\0"
	NEPIPE "Broken pipe\0"
	NEDOM  "Math argument out of domain of func\0"
	NERANGE "Math result not representable\0"
	NEDEADLK "Resource deadlock would occur\0"
	NENAMETOOLONG "File name too long\0"
	NENOLCK "No record locks available\0"
	NENOSYS "Function not implemented\0"
	NENOTEMPTY "Directory not empty\0"
	NELOOP "Too many symbolic links encountered\0"
	NEWOULDBLOCK "Operation would block\0"
	NENOMSG "No message of desired type\0"
	NEIDRM "Identifier removed\0"
	NECHRNG "Channel number out of range\0"
	NEL2NSYNC "Level 2 not synchronized\0"
	NEL3HLT "Level 3 halted\0"
	NEL3RST "Level 3 reset\0"
	NELNRNG "Link number out of range\0"
	NEUNATCH "Protocol driver not attached\0"
	NENOCSI "No CSI structure available\0"
	NEL2HLT "Level 2 halted\0"
	NEBADE "Invalid exchange\0"
	NEBADR "Invalid request descriptor\0"
	NEXFULL "Exchange full\0"
	NENOANO "No anode\0"
	NEBADRQC "Invalid request code\0"
	NEBADSLT "Invalid slot\0"
	NEDEADLOCK "Resource deadlock would occur\0"
	NEBFONT "Bad font file format\0"
	NENOSTR "Device not a stream\0"
	NENODATA "No data available\0"
	NETIME "Timer expired\0"
	NENOSR "Out of streams resources\0"
	NENONET "Machine is not on the network\0"
	NENOPKG "Package not installed\0"
	NEREMOTE "Object is remote\0"
	NENOLINK "Link has been severed\0"
	NEADV  "Advertise error\0"
	NESRMNT "Srmount error\0"
	NECOMM "Communication error on send\0"
	NEPROTO "Protocol error\0"
	NEMULTIHOP "Multihop attempted\0"
	NEDOTDOT "RFS specific error\0"
	NEBADMSG "Not a data message\0"
	NEOVERFLOW "Value too large for defined data type\0"
	NENOTUNIQ "Name not unique on network\0"
	NEBADFD "File descriptor in bad state\0"
	NEREMCHG "Remote address changed\0"
	NELIBACC "Can not access a needed shared library\0"
	NELIBBAD "Accessing a corrupted shared library\0"
	NELIBSCN ".lib section in a.out corrupted\0"
	NELIBMAX "Attempting to link in too many shared libraries\0"
	NELIBEXEC "Cannot exec a shared library directly\0"
	NEILSEQ "Illegal byte sequence\0"
	NERESTART "Interrupted system call should be restarted\0"
	NESTRPIPE "Streams pipe error\0"
	NEUSERS "Too many users\0"
	NENOTSOCK "Socket operation on non-socket\0"
	NEDESTADDRREQ "Destination address required\0"
	NEMSGSIZE "Message too long\0"
	NEPROTOTYPE "Protocol wrong type for socket\0"
	NENOPROTOOPT "Protocol not available\0"
	NEPROTONOSUPPORT"Protocol not supported\0"
	NESOCKTNOSUPPORT"Socket type not supported\0"
	NEOPNOTSUPP "Operation not supported on transport endpoint\0"
	NEPFNOSUPPORT "Protocol family not supported\0"
	NEAFNOSUPPORT "Address family not supported by protocol\0"
	NEADDRINUSE "Address already in use\0"
	NEADDRNOTAVAIL"Cannot assign requested address\0"
	NENETDOWN "Network is down\0"
	NENETUNREACH "Network is unreachable\0"
	NENETRESET "Network dropped connection because of reset\0"
	NECONNABORTED "Software caused connection abort\0"
	NECONNRESET "Connection reset by peer\0"
	NENOBUFS "No buffer space available\0"
	NEISCONN "Transport endpoint is already connected\0"
	NENOTCONN "Transport endpoint is not connected\0"
	NESHUTDOWN "Cannot send after transport endpoint shutdown\0"
	NETOOMANYREFS "Too many references: cannot splice\0"
	NETIMEDOUT "Connection timed out\0"
	NECONNREFUSED "Connection refused\0"
	NEHOSTDOWN "Host is down\0"
	NEHOSTUNREACH "No route to host\0"
	NEALREADY "Operation already in progress\0"
	NEINPROGRESS "Operation now in progress\0"
	NESTALE "Stale NFS file handle\0"
	NEUCLEAN "Structure needs cleaning\0"
	NENOTNAM "Not a XENIX named type file\0"
	NENAVAIL "No XENIX semaphores available\0"
	NEISNAM "Is a named type file\0"
	NEREMOTEIO "Remote I/O error\0"
	NENOMEDIUM "No medium found\0"
	NEMEDIUMTYPE "Wrong medium type\0"
	NECANCELED "Operation Canceled\0"
	NENOKEY "Required key not available\0"
	NEKEYEXPIRED "Key has expired\0"
	NEKEYREVOKED "Key has been revoked\0"
	NEKEYREJECTED "Key was rejected by service\0"
#ifdef NEDQUOT
	NEDQUOT "Quota exceeded\0"
#endif
	"\0";

static int error_code(const char* p)
{
	return *((unsigned char*) p);
}

static const char* error_name(const char* p)
{
	p += 1;
	return p;
}

static const char* error_message(const char* p)
{
	p += 1;
	p += strlen(p) + 1;
	return p;
}

static const char* next(const char* p)
{
	p += 1;
	p += strlen(p) + 1;
	p += strlen(p) + 1;
	return p;
}

static const char* find_by_name(char* name)
{
	const char* p;

	for(p = errors; *p; p = next(p))
		if(!strcmp(error_name(p), name))
			return p;

	return NULL;
}

static const char* find_by_code(int code)
{
	const char* p;

	for(p = errors; *p; p = next(p))
		if(error_code(p) == code)
			return p;

	return NULL;
}

static void fail_unknown(const char* arg)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, TAG ": unknown error ");
	p = fmtstr(p, e, arg);
	FMTENL(p, e);

	sys_write(STDERR, buf, p - buf);

	_exit(-1);
}

static void dump_error(const char* er, int code)
{
	const char* message = error_message(er);

	FMTBUF(p, e, buf, strlen(message) + 20);

	p = fmtstr(p, e, message);
	p = fmtstr(p, e, " (");

	if(code)
		p = fmtstr(p, e, error_name(er));
	else
		p = fmtint(p, e, error_code(er));

	p = fmtstr(p, e, ")");

	FMTENL(p, e);

	sys_write(STDOUT, buf, p - buf);
};

static int atoi(char* a)
{
	int err;
	char* p;

	if(!(p = parseint(a, &err)) || *p)
		return -1;

	return err;
}

static void report_error(char* arg)
{
	const char* er = NULL;
	int code = 0;

	if(*arg == 'E')
		er = find_by_name(arg);
	else if(*arg >= '0' && *arg <= '9')
		er = find_by_code(code = atoi(arg));
	if(!er)
		fail_unknown(arg);

	dump_error(er, code);
}

int main(int argc, char** argv)
{
	int i;

	for(i = 1; i < argc; i++)
		report_error(argv[i]);

	return 0;
}
