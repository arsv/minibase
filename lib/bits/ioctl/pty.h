#include <bits/ioctl.h>

#define TIOCSPTLCK      _IOW('T', 0x31, int)
#define TIOCSIG         _IOW('T', 0x36, int)
#define TIOCGPKT        _IOR('T', 0x38, int)
#define TIOCGPTLCK      _IOR('T', 0x39, int)
#define TIOCGEXCL       _IOR('T', 0x40, int)
#define TIOCGPTPEER     _IO('T', 0x41)
