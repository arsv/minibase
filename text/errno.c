#include <bits/errno.h>
#include <sys/write.h>

#include <null.h>
#include <strlen.h>
#include <strcmp.h>
#include <fmtstr.h>
#include <memcpy.h>

#define TAG "errno"
#define ESUCCESS 0

static const struct err {
	short code;
	char* name;
	char* message;
} errors[] = {
#define e(a, b) { a, #a, b }
/*        01234567890123456 */
	e(ESUCCESS, 	"Success"),
	e(EPERM,	"Operation not permitted"),
	e(ENOENT,	"No such file or directory"),
	e(ESRCH,	"No such process"),
	e(EINTR,	"Interrupted system call"),
	e(EIO,		"I/O error"),
	e(ENXIO,	"No such device or address"),
	e(E2BIG,	"Arg list too long"),
	e(ENOEXEC,	"Exec format error"),
	e(EBADF,	"Bad file number"),
	e(ECHILD,	"No child processes"),
	e(EAGAIN,	"Try again"),
	e(ENOMEM,	"Out of memory"),
	e(EACCES,	"Permission denied"),
	e(EFAULT,	"Bad address"),
	e(ENOTBLK,	"Block device required"),
	e(EBUSY,	"Device or resource busy"),
	e(EEXIST,	"File exists"),
	e(EXDEV,	"Cross-device link"),
	e(ENODEV,	"No such device"),
	e(ENOTDIR,	"Not a directory"),
	e(EISDIR,	"Is a directory"),
	e(EINVAL,	"Invalid argument"),
	e(ENFILE,	"File table overflow"),
	e(EMFILE,	"Too many open files"),
	e(ENOTTY,	"Not a typewriter"),
	e(ETXTBSY,	"Text file busy"),
	e(EFBIG,	"File too large"),
	e(ENOSPC,	"No space left on device"),
	e(ESPIPE,	"Illegal seek"),
	e(EROFS,	"Read-only file system"),
	e(EMLINK,	"Too many links"),
	e(EPIPE,	"Broken pipe"),
	e(EDOM,		"Math argument out of domain of func"),
	e(ERANGE,	"Math result not representable"),
	e(EDEADLK,	"Resource deadlock would occur"),
	e(ENAMETOOLONG,	"File name too long"),
	e(ENOLCK,	"No record locks available"),
	e(ENOSYS,	"Function not implemented"),
	e(ENOTEMPTY,	"Directory not empty"),
	e(ELOOP,	"Too many symbolic links encountered"),
	e(EWOULDBLOCK,	"Operation would block"),
	e(ENOMSG,	"No message of desired type"),
	e(EIDRM,	"Identifier removed"),
	e(ECHRNG,	"Channel number out of range"),
	e(EL2NSYNC,	"Level 2 not synchronized"),
	e(EL3HLT,	"Level 3 halted"),
	e(EL3RST,	"Level 3 reset"),
	e(ELNRNG,	"Link number out of range"),
	e(EUNATCH,	"Protocol driver not attached"),
	e(ENOCSI,	"No CSI structure available"),
	e(EL2HLT,	"Level 2 halted"),
	e(EBADE,	"Invalid exchange"),
	e(EBADR,	"Invalid request descriptor"),
	e(EXFULL,	"Exchange full"),
	e(ENOANO,	"No anode"),
	e(EBADRQC,	"Invalid request code"),
	e(EBADSLT,	"Invalid slot"),
	e(EDEADLOCK,	"Resource deadlock would occur")	/* =EDEADLK */,
	e(EBFONT,	"Bad font file format"),
	e(ENOSTR,	"Device not a stream"),
	e(ENODATA,	"No data available"),
	e(ETIME,	"Timer expired"),
	e(ENOSR,	"Out of streams resources"),
	e(ENONET,	"Machine is not on the network"),
	e(ENOPKG,	"Package not installed"),
	e(EREMOTE,	"Object is remote"),
	e(ENOLINK,	"Link has been severed"),
	e(EADV,		"Advertise error"),
	e(ESRMNT,	"Srmount error"),
	e(ECOMM,	"Communication error on send"),
	e(EPROTO,	"Protocol error"),
	e(EMULTIHOP,	"Multihop attempted"),
	e(EDOTDOT,	"RFS specific error"),
	e(EBADMSG,	"Not a data message"),
	e(EOVERFLOW,	"Value too large for defined data type"),
	e(ENOTUNIQ,	"Name not unique on network"),
	e(EBADFD,	"File descriptor in bad state"),
	e(EREMCHG,	"Remote address changed"),
	e(ELIBACC,	"Can not access a needed shared library"),
	e(ELIBBAD,	"Accessing a corrupted shared library"),
	e(ELIBSCN,	".lib section in a.out corrupted"),
	e(ELIBMAX,	"Attempting to link in too many shared libraries"),
	e(ELIBEXEC,	"Cannot exec a shared library directly"),
	e(EILSEQ,	"Illegal byte sequence"),
	e(ERESTART,	"Interrupted system call should be restarted"),
	e(ESTRPIPE,	"Streams pipe error"),
	e(EUSERS,	"Too many users"),
	e(ENOTSOCK,	"Socket operation on non-socket"),
	e(EDESTADDRREQ,	"Destination address required"),
	e(EMSGSIZE,	"Message too long"),
	e(EPROTOTYPE,	"Protocol wrong type for socket"),
	e(ENOPROTOOPT,	"Protocol not available"),
	e(EPROTONOSUPPORT,"Protocol not supported"),
	e(ESOCKTNOSUPPORT,"Socket type not supported"),
	e(EOPNOTSUPP,	"Operation not supported on transport endpoint"),
	e(ENOTSUP,	"Operation not supported on transport endpoint") /* dupe */,
	e(EPFNOSUPPORT,	"Protocol family not supported"),
	e(EAFNOSUPPORT,	"Address family not supported by protocol"),
	e(EADDRINUSE,	"Address already in use"),
	e(EADDRNOTAVAIL,"Cannot assign requested address"),
	e(ENETDOWN,	"Network is down"),
	e(ENETUNREACH,	"Network is unreachable"),
	e(ENETRESET,	"Network dropped connection because of reset"),
	e(ECONNABORTED,	"Software caused connection abort"),
	e(ECONNRESET,	"Connection reset by peer"),
	e(ENOBUFS,	"No buffer space available"),
	e(EISCONN,	"Transport endpoint is already connected"),
	e(ENOTCONN,	"Transport endpoint is not connected"),
	e(ESHUTDOWN,	"Cannot send after transport endpoint shutdown"),
	e(ETOOMANYREFS,	"Too many references: cannot splice"),
	e(ETIMEDOUT,	"Connection timed out"),
	e(ECONNREFUSED,	"Connection refused"),
	e(EHOSTDOWN,	"Host is down"),
	e(EHOSTUNREACH,	"No route to host"),
	e(EALREADY,	"Operation already in progress"),
	e(EINPROGRESS,	"Operation now in progress"),
	e(ESTALE,	"Stale NFS file handle"),
	e(EUCLEAN,	"Structure needs cleaning"),
	e(ENOTNAM,	"Not a XENIX named type file"),
	e(ENAVAIL,	"No XENIX semaphores available"),
	e(EISNAM,	"Is a named type file"),
	e(EREMOTEIO,	"Remote I/O error"),
	e(EDQUOT,	"Quota exceeded"),
	e(ENOMEDIUM,	"No medium found"),
	e(EMEDIUMTYPE,	"Wrong medium type"),
	e(ECANCELED,	"Operation Canceled"),
	e(ENOKEY,	"Required key not available"),
	e(EKEYEXPIRED,	"Key has expired"),
	e(EKEYREVOKED,	"Key has been revoked"),
	e(EKEYREJECTED,	"Key was rejected by service"),
	{ 0, "", NULL }
};

static const struct err* findbyname(const char* name)
{
	const struct err* e;

	for(e = errors; e->message; e++)
		if(!strcmp(e->name, name))
			return e;

	return NULL;
}

static const struct err* findbycode(int code)
{
	const struct err* e;

	for(e = errors; e->message; e++)
		if(e->code == code)
			return e;

	return NULL;
}

static void unknown(const char* obj)
{
	char buf[100];
	char* end = buf + sizeof(buf) - 1; 
	char* p = buf;

	p = fmtstr(p, end, TAG ": unknown error ");
	p = fmtstr(p, end, obj);
	*p++ = '\n';

	syswrite(2, buf, p - buf);
}

static void writeline(const char* msg)
{
	int len = strlen(msg);
	char buf[len + 2];

	memcpy(buf, msg, len);
	buf[len+0] = '\n';
	buf[len+1] = '\0';

	syswrite(1, buf, len+1);
};

/* Unlike other tools, strerror does not use fail() with its
   ERRLIST, which makes brinding in any kind of error-checking
   generic atoi implementation problematic. */

static int atoerr(const char* a)
{
	int d, err = 0;

	for(; *a; a++) {
		if(*a >= '0' && ((d = *a - '0') <= 9))
			err = (err*10) + d;
		else
			return -1;
		if(err >= 2048)
			return -1;
	}

	return err;
}

static void reporterror(const char* arg)
{
	const struct err* e = NULL;

	if(*arg == 'E')
		e = findbyname(arg);
	else if(*arg >= '0' && *arg <= '9')
		e = findbycode(atoerr(arg));
	if(e)
		writeline(e->message);
	else
		unknown(arg);
}

int main(int argc, char** argv)
{
	int i;

	for(i = 1; i < argc; i++)
		reporterror(argv[i]);

	return 0;
}
