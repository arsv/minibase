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
#define O_DIRECTORY	(1<<16)
#define O_NOFOLLOW	(1<<17)
#define O_NOATIME	(1<<18)
#define O_FSYNC		(1<<20)
#define O_SYNC		(O_FSYNC | O_DSYNC)
#define O_PATH		(1<<21)

#define S_IFMT		0170000 /* Mask */
#define	S_IFDIR		0040000	/* Directory.  */
#define	S_IFCHR		0020000	/* Character device.  */
#define	S_IFBLK		0060000	/* Block device.  */
#define	S_IFREG		0100000	/* Regular file.  */
#define	S_IFIFO		0010000	/* FIFO.  */
#define	S_IFLNK		0120000	/* Symbolic link.  */
#define	S_IFSOCK	0140000	/* Socket.  */

#define AT_FDCWD		-100
#define AT_REMOVEDIR		0x200
#define AT_SYMLINK_NOFOLLOW	0x100
#define AT_EMPTY_PATH		0x1000

#endif
