#include <bits/types.h>

/* In Linux, size_t is always unsigned while off_t is always *signed*.
   This *will* trigger strict signed-unsigned checks in a way that *cannot*
   be avoided any time size in memory is compared to size on disk.
   Ad-hoc casts to fix the warnings will only make the situation worse:
   32 < 63 on 32-bit arches but 64 > 63 on 64-bit arches.

   The following is a reasonably safe way to avoid signed-unsigned compare
   warnings. It will not help with assignments however, those must be handled
   *very* carefully.

   The proper way to fix this is to make file sizes unsigned and change seek
   modes to { SEEK_SET, SEEK_FWD, SEEK_REV, SEEK_END }, all taking unsigned
   offsets. Too bad the wrong decision is already burned into the syscall
   interface.

   Return: -1 if msz < fsz, +1 if msz > fsz. */

inline static int mem_off_cmp(size_t msz, off_t fsz)
{
	if(fsz < 0)
		return 1;

#if BITS == 32 /* sizeof(msz) < sizeof(fsz) */
	off_t smsz = (off_t)msz;

	if(smsz < fsz)
		return -1;
	if(smsz > fsz)
		return 1;
#else /* sizeof(msz) == sizeof(fsz) */
	size_t ufsz = (size_t)fsz;

	if(msz < ufsz)
		return -1;
	if(msz > ufsz)
		return 1;
#endif

	return 0;
}
