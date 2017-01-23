#ifndef __BITS_MMAN_H__
#define __BITS_MMAN_H__

#define PROT_READ       (1<<0)
#define PROT_WRITE      (1<<1)
#define PROT_EXEC       (1<<2)

#define MAP_SHARED      (1<<0)
#define MAP_PRIVATE     (1<<1)
#define MAP_FIXED       (1<<4)
#define MAP_ANONYMOUS   (1<<5)

#define MREMAP_MAYMOVE  (1<<0)
#define MREMAP_FIXED    (1<<1)

#define MMAPERROR(ret) ( ((ret) < 0) && ((ret) > -2048) )

#endif
