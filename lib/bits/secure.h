#ifndef __BITS_SECURE_H__
#define __BITS_SECURE_H__

/* Used by prctl(2) but these are not PR_*, so let's keep them separated. */

#define SECURE_NOROOT                0
#define SECURE_NO_SETUID_FIXUP       2
#define SECURE_KEEP_CAPS             4
#define SECURE_NO_CAP_AMBIENT_RAISE  6

#define SECBIT_NOROOT               (1<<0)
#define SECBIT_NO_SETUID_FIXUP      (1<<2)
#define SECBIT_KEEP_CAPS            (1<<4)
#define SECBIT_NO_CAP_AMBIENT_RAISE (1<<6)

#endif
