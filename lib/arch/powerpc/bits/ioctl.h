#ifndef __BITS_IOCTL_H__
#define __BITS_IOCTL_H__

#define _IOC_N 1U
#define _IOC_R 2U
#define _IOC_W 4U
#define _IOC_RW (_IOC_R | _IOC_W)

#define _IOC(a,b,c,d) ( ((a)<<29) | ((b)<<8) | (c) | ((d)<<16) )

#define _IO(a,b)     _IOC(_IOC_N,(a),(b),0)
#define _IOW(a,b,c)  _IOC(_IOC_W,(a),(b),sizeof(c))
#define _IOR(a,b,c)  _IOC(_IOC_R,(a),(b),sizeof(c))
#define _IOWR(a,b,c) _IOC(_IOC_RW,(a),(b),sizeof(c))

#endif
