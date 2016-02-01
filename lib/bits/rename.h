#ifndef __BITS_RENAME_H__
#define __BITS_RENAME_H__

/* uapi/linux/fs.h */

#define RENAME_NOREPLACE	(1 << 0)	/* Don't overwrite target */
#define RENAME_EXCHANGE		(1 << 1)	/* Exchange source and dest */
#define RENAME_WHITEOUT		(1 << 2)	/* Whiteout source */

#endif
