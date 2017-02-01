#include <bits/ioctl.h>

#define EVIOCGRAB      _IOW('E', 0x90, int)
#define EVIOCREVOKE    _IOW('E', 0x91, int)
