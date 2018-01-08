#ifndef __BITS_IOCTL_H__
#define __BITS_IOCTL_H__

#define _IOC_W  1UL
#define _IOC_R  2UL
#define _IOC_RW 3UL

#define _IOC(dir,type,nr,size) \
        ( ((dir)<<30) | ((type)<<8) | (nr) | ((size)<<16) )

#define _IO(type,nr)        _IOC(0,(type),(nr),0)
#define _IOR(type,nr,size)  _IOC(_IOC_R,(type),(nr),sizeof(size))
#define _IOW(type,nr,size)  _IOC(_IOC_W,(type),(nr),sizeof(size))
#define _IOWR(type,nr,size) _IOC(_IOC_RW,(type),(nr),sizeof(size))

#endif
