#ifndef __BITS_MMAN_H__
#define __BITS_MMAN_H__

#define PROT_READ	(1<<0)		/* page can be read */
#define PROT_WRITE	(1<<1)		/* page can be written */
#define PROT_EXEC	(1<<2)		/* page can be executed */

#define MAP_SHARED	(1<<0)		/* Share changes */
#define MAP_PRIVATE	(1<<1)		/* Changes are private */
#define MAP_FIXED	(1<<4)		/* Interpret addr exactly */
#define MAP_ANONYMOUS	(1<<5)		/* don't use a file */

#define MMAPERROR(ret) ( ((ret) < 0) && ((ret) > -2048) )

#endif
