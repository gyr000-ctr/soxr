/* SoX Resampler Library      Copyright (c) 2007-18 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

/* Buffer extent and overlap helpers, used by the implementation in soxr.c. */

#if !defined soxr_data_types_included
#define soxr_data_types_included

#include <stddef.h>

#include "std-types.h"



/* Error reported (via soxr_error) for invalid buffer pointers, and for
 * buffer lengths that cannot be represented. */

#define SOXR_EBADPTR "invalid buffer pointer"



/* An integer type able to hold an object pointer without loss. */

#if HAVE_STDINT_H && defined UINTPTR_MAX
  typedef uintptr_t soxr_uintptr_t;
#elif defined __STDC_VERSION__ && __STDC_VERSION__ >= 199901L
  typedef unsigned long long soxr_uintptr_t;
#else
  typedef unsigned long soxr_uintptr_t;
#endif



/* The relationship between two byte ranges.  `FULL' is the exact in-place
 * case (both ranges begin at the same address); `PARTIAL' covers every other
 * intersection, including one range wholly containing the other.  Empty
 * ranges never overlap anything. */

typedef enum {
  SOXR_NO_OVERLAP = 0,
  SOXR_FULL_OVERLAP,
  SOXR_PARTIAL_OVERLAP
} soxr_overlap_t;



static soxr_overlap_t soxr_overlap(void const * a, size_t a_len,
    void const * b, size_t b_len)
{
  soxr_uintptr_t const a0 = (soxr_uintptr_t)(void const *)a;
  soxr_uintptr_t const b0 = (soxr_uintptr_t)(void const *)b;

  if (!a_len || !b_len)
    return SOXR_NO_OVERLAP;
  if (a0 == b0)
    return SOXR_FULL_OVERLAP;
  if (a0 < b0)
    return b0 - a0 < (soxr_uintptr_t)a_len?
        SOXR_PARTIAL_OVERLAP : SOXR_NO_OVERLAP;
  return a0 - b0 < (soxr_uintptr_t)b_len?
      SOXR_PARTIAL_OVERLAP : SOXR_NO_OVERLAP;
}



/* Overflow-checked arithmetic on sizes. */

static bool soxr_size_mul(size_t a, size_t b, size_t * result)
{
  if (b && a > (size_t)-1 / b)
    return false;
  *result = a * b;
  return true;
}



static bool soxr_size_add(size_t a, size_t b, size_t * result)
{
  if (a > (size_t)-1 - b)
    return false;
  *result = a + b;
  return true;
}



/* Round a byte count up to a multiple of the strictest alignment that any
 * sample datatype (or a pointer) can require. */

typedef union {
  void * p;
  double d;
  long double ld;
} soxr_alignment_t;

#define SOXR_ALIGNMENT ((size_t)sizeof(soxr_alignment_t))

static bool soxr_size_align(size_t n, size_t * result)
{
  if (!soxr_size_add(n, SOXR_ALIGNMENT - 1, result))
    return false;
  *result /= SOXR_ALIGNMENT;
  return soxr_size_mul(*result, SOXR_ALIGNMENT, result);
}

#endif
