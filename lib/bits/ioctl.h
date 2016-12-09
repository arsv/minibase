#ifndef __BITS_IOCTL_H__
#define __BITS_IOCTL_H__

#define _IOC_W	1
#define _IOC_R	2

#define _IOC(dir,type,nr,size) \
	( ((dir)<<30) | ((type)<<8) | (nr) | ((size)<<16) )

#define _IOR(type,nr,size)	_IOC(_IOC_R,(type),(nr),sizeof(size))
#define _IOW(type,nr,size)	_IOC(_IOC_W,(type),(nr),sizeof(size))
#define _IOWR(type,nr,size)	_IOC(_IOC_R|_IOC_W,(type),(nr),sizeof(size))

#endif
