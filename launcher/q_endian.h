/*
	q_endian.h
	endianness handling

	$Id: q_endian.h,v 1.4 2007-10-21 15:25:23 sezero Exp $
*/

#ifndef __QENDIAN_H
#define __QENDIAN_H

/*
 * endianness stuff: <sys/types.h> is supposed
 * to succeed in locating the correct endian.h
 * this BSD style may not work everywhere.
 */

#undef ENDIAN_GUESSED_SAFE
#undef ENDIAN_ASSUMED_UNSAFE


#include <sys/types.h>

/* include more if it didn't work: */
#if !defined(BYTE_ORDER)

# if defined(__linux__) || defined(__linux)
#	include <endian.h>
# elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#	include <machine/endian.h>
# elif defined(__sun) || defined(__svr4__)
#	include <sys/byteorder.h>
# elif defined(_AIX)
#	include <sys/machine.h>
# elif defined(sgi)
#	include <sys/endian.h>
# elif defined(__DJGPP__)
#	include <machine/endian.h>
# endif

#endif	/* endian includes */


#if defined(__BYTE_ORDER) && !defined(BYTE_ORDER)
#define	BYTE_ORDER	__BYTE_ORDER
#endif	/* __BYTE_ORDER */

#if !defined(PDP_ENDIAN)
#if defined(__PDP_ENDIAN)
#define	PDP_ENDIAN	__PDP_ENDIAN
#else
#define	PDP_ENDIAN	3412
#endif
#endif	/* the NUXI endian, not supported actually.. */

#if defined(__LITTLE_ENDIAN) && !defined(LITTLE_ENDIAN)
#define	LITTLE_ENDIAN	__LITTLE_ENDIAN
#endif	/* __LITTLE_ENDIAN */

#if defined(__BIG_ENDIAN) && !defined(BIG_ENDIAN)
#define	BIG_ENDIAN	__BIG_ENDIAN
#endif	/* __LITTLE_ENDIAN */


#if defined(BYTE_ORDER) && defined(LITTLE_ENDIAN) && defined(BIG_ENDIAN)

# if (BYTE_ORDER != LITTLE_ENDIAN) && (BYTE_ORDER != BIG_ENDIAN) && (BYTE_ORDER != PDP_ENDIAN)
# error "Unsupported endianness."
# endif

#else	/* one of the definitions is mising. */

# undef BYTE_ORDER
# undef LITTLE_ENDIAN
# undef BIG_ENDIAN
# undef PDP_ENDIAN
# define LITTLE_ENDIAN	1234
# define BIG_ENDIAN	4321
# define PDP_ENDIAN	3412

#endif	/* byte order defs */


#if !defined(BYTE_ORDER)
/* supposedly safe assumptions: these may actually
 * be OS dependant and listing all possible compiler
 * macros here is impossible (the ones here are gcc
 * flags, mostly.) so, proceed carefully..
 */

# if defined(__DJGPP__) || defined(MSDOS) || defined(__MSDOS__)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* DOS */

# elif defined(__sun) || defined(__svr4__)	/* solaris */
#   if defined(_LITTLE_ENDIAN)	/* x86 */
#	define	BYTE_ORDER	LITTLE_ENDIAN
#   elif defined(_BIG_ENDIAN)	/* sparc */
#	define	BYTE_ORDER	BIG_ENDIAN
#   endif

# elif defined(__i386) || defined(__i386__) ||	/* any x86 */	\
       defined(_M_IX86) ||					\
       defined(__amd64) || defined(__x86_64__)	/* any x64 */
#	define	BYTE_ORDER	LITTLE_ENDIAN

# elif defined (__ppc__) || defined(__POWERPC__) || defined(_M_PPC)
#	define	BYTE_ORDER	BIG_ENDIAN	/* PPC: big endian */

# elif (defined(__alpha__) || defined(__alpha)) || defined(_M_ALPHA)
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* should be safe */

# elif defined(_WIN32) || defined(_WIN64)	/* windows : */
#	define	BYTE_ORDER	LITTLE_ENDIAN	/* should be safe */

# elif defined(__hppa__) || defined(__sparc__)	/* others: check! */
#	define	BYTE_ORDER	BIG_ENDIAN

# endif

# if defined(BYTE_ORDER)
  /* raise a flag, just in case: */
#	define	ENDIAN_GUESSED_SAFE	BYTE_ORDER
# endif

#endif	/* supposedly safe assumptions */


#if !defined(BYTE_ORDER)

/* brain-dead fallback: default to little endian.
 * change if necessary!!!!
 */
# define BYTE_ORDER	LITTLE_ENDIAN
# define ENDIAN_ASSUMED_UNSAFE		BYTE_ORDER

#endif	/* fallback. */


extern int DetectByteorder (void);

extern short	ShortSwap (short);
extern int	LongSwap (int);
extern float	FloatSwap (float);

extern int	LongSwapPDP2BE (int);
extern int	LongSwapPDP2LE (int);
extern float	FloatSwapPDP2BE (float);
extern float	FloatSwapPDP2LE (float);


/* byte swapping. most times we want to convert to
 * little endian: our data files are written in LE
 * format.  sometimes, such as when writing to net,
 * we also convert to big endian.
*/
#if (BYTE_ORDER == BIG_ENDIAN)

#define BigShort(s)	(s)
#define LittleShort(s)	ShortSwap((s))
#define BigLong(l)	(l)
#define LittleLong(l)	LongSwap((l))
#define BigFloat(f)	(f)
#define LittleFloat(f)	FloatSwap((f))

#elif (BYTE_ORDER == PDP_ENDIAN)

#define BigShort(s)	ShortSwap((s))
#define BigLong(l)	LongSwapPDP2BE((l))
#define BigFloat(f)	FloatSwapPDP2BE((f))
#define LittleShort(s)	(s)
#define LittleLong(l)	LongSwapPDP2LE((l))
#define LittleFloat(f)	FloatSwapPDP2LE((f))

#else /* BYTE_ORDER == LITTLE_ENDIAN */

#define BigShort(s)	ShortSwap((s))
#define LittleShort(s)	(s)
#define BigLong(l)	LongSwap((l))
#define LittleLong(l)	(l)
#define BigFloat(f)	FloatSwap((f))
#define LittleFloat(f)	(f)

#endif	/* swap macros */

#endif	/* __QENDIAN_H */

