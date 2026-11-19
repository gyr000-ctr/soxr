/* SoX Resampler Library      Copyright (c) 2007-18 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

/* Packed 24-bit sample conversion.
 *
 * A packed 24-bit sample occupies three bytes and holds a signed
 * two's-complement value in [-2^23, 2^23-1].  The bytes are the three
 * least-significant bytes of the sample's 32-bit representation, in the
 * machine's native byte order -- the same convention as the library's other
 * integer datatypes, which are held as native-endian values.  Thus on a
 * little-endian machine the least-significant byte comes first; on a
 * big-endian machine the most-significant of the three comes first. */

#if !defined soxr_int24_included
#define soxr_int24_included

#include "rint.h" /* For rint32D: the same rounding as the 32-bit I/O path. */
#include "std-types.h"



/* Convert the packed sample at `p' to a sign-extended 32-bit integer. */

static __inline int32_t soxr_int24_get(void const * p)
{
  unsigned char const * b = p;
  uint32_t u;
#if HAVE_BIGENDIAN
  u = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
#else
  u = ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[0];
#endif
  return (u & 0x800000u)? (int32_t)u - 0x1000000 : (int32_t)u;
}



/* Store `v' (which must be in [-2^23, 2^23-1]) packed. */

static __inline void soxr_int24_put(void * p, int32_t v)
{
  unsigned char * b = p;
  uint32_t u = (uint32_t)v & 0xffffffu;
#if HAVE_BIGENDIAN
  b[0] = (unsigned char)(u >> 16);
  b[1] = (unsigned char)(u >> 8);
  b[2] = (unsigned char)u;
#else
  b[0] = (unsigned char)u;
  b[1] = (unsigned char)(u >> 8);
  b[2] = (unsigned char)(u >> 16);
#endif
}



/* Round `d' (in 24-bit units) to the nearest integer, as the 32-bit path
 * rounds to int32_t, and saturate to [-2^23, 2^23-1], counting a clip when
 * the rounded value does not fit.
 *
 * `rint32D' is used so that the rounding (including its tie-breaking) is
 * exactly that of the 32-bit path on this platform.  It is applied only to
 * values small enough for the result certainly to be in range: a conversion
 * out of int32_t's range is not defined, and on some platforms raises the
 * invalid-operation flag, which other code would then see.  Anything larger
 * is clipped without it.  NaN is treated as the most-negative value, as in
 * the 32-bit path. */

static __inline int32_t soxr_int24_rint_clip(double d, size_t * clips)
{
  int32_t v;

  if (d > -8388609. && d < 8388608.) {
    rint32D(v, d);
    if (v >= -8388608 && v <= 8388607)
      return v;
  }
  ++*clips;
  return d > 0? 8388607 : -8388608;
}

#endif
