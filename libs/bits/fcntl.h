#ifndef __BITS_FCNTL_H__
#define __BITS_FCNTL_H__

#define O_RDONLY	(0<<0)
#define O_WRONLY	(1<<0)
#define O_RDWR		(1<<1)
#define O_CREAT		(1<<6)
#define O_EXCL		(1<<7)
#define O_NOCTTY	(1<<8)
#define O_TRUNC		(1<<9)
#define O_APPEND	(1<<10)
#define O_NONBLOCK	(1<<11)
#define O_DSYNC		(1<<12)
#define FASYNC		(1<<13)
#define O_DIRECT	(1<<14)

#endif
