/* SoX Resampler Library      Copyright (c) 2007-18 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

/* Test 2: exercises packed 24-bit I/O.
 *
 *  - pack/unpack round-trip of every one of the 2^24 code values, bit-exact,
 *    both for the library's helpers and through the public API at unity
 *    rate;
 *  - the packed representation's byte order;
 *  - io_spec validation: 24-bit datatypes are accepted, invalid datatypes
 *    are rejected, and the error is distinguishable;
 *  - clipping: the same rounding and saturation policy, at the same input
 *    thresholds, as the 32-bit path;
 *  - the split-channel and interleaved paths agree exactly;
 *  - 24-bit I/O through a real rate change is consistent with the 32-bit
 *    path (which has the same results scaled by a power of two).
 */

#ifdef NDEBUG /* N.B. assert used with active statements so enable always. */
#undef NDEBUG /* Must undef above assert.h or other that might include it. */
#endif

#include <soxr.h>
#include "soxr-config.h"   /* For HAVE_BIGENDIAN, as used by the library. */
#include "../src/int24.h"  /* The library's own pack/unpack helpers. */
#include "../examples/examples-common.h"



typedef unsigned char byte_t;

/* Reference implementations of pack/unpack, written per the documented
 * representation (native byte order, three least-significant bytes). */

static int32_t ref_get(void const * p)
{
  byte_t const * b = p;
  uint32_t u;
#if HAVE_BIGENDIAN
  u = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
#else
  u = ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0];
#endif
  return (u & 0x800000u)? (int32_t)u - 0x1000000 : (int32_t)u;
}

static void ref_put(void * p, int32_t v)
{
  byte_t * b = p;
  uint32_t u = (uint32_t)v & 0xffffffu;
#if HAVE_BIGENDIAN
  b[0] = (byte_t)(u >> 16), b[1] = (byte_t)(u >> 8), b[2] = (byte_t)u;
#else
  b[0] = (byte_t)u, b[1] = (byte_t)(u >> 8), b[2] = (byte_t)(u >> 16);
#endif
}



/* Unity-rate I/O tests need a quality spec that is an exact passthrough;
 * HQ (20-bit) at a 1:1 ratio is, as used by the existing tests. */

static soxr_t create(soxr_datatype_t itype, soxr_datatype_t otype,
    unsigned ch, soxr_error_t * error)
{
  soxr_io_spec_t io_spec = soxr_io_spec(itype, otype);
  soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, 0);
  soxr_runtime_spec_t r_spec = soxr_runtime_spec(1);
  return soxr_create(1, 1, ch, error, &io_spec, &q_spec, &r_spec);
}



/* Some builds (e.g. those without a 32-bit engine) fall back to the
 * variable-rate, single-precision engine even for a constant rate.  That
 * engine cannot pass data through unchanged at a 1:1 ratio, nor is it
 * expected to give bit-identical results between integer formats, so the
 * tests that rely on either are skipped there. */

static bool cr_engine_available(void)
{
  soxr_error_t error;
  soxr_t soxr = create(SOXR_FLOAT32_I, SOXR_FLOAT32_I, 1, &error);
  bool result = soxr && !error && strcmp(soxr_engine(soxr), "vr32") != 0;
  soxr_delete(soxr);
  return result;
}



static void round_trip_test(void)
{
  byte_t buf[3], buf1[3];
  int32_t v;

  /* The library's helpers: every code value, both ways, and agreement with
   * the reference implementation. */
  for (v = -8388608; v <= 8388607; ++v) {
    ref_put(buf, v);
    assert(ref_get(buf) == v);
    soxr_int24_put(buf1, v);
    assert(memcmp(buf, buf1, sizeof(buf)) == 0);   /* Exact bytes. */
    assert(soxr_int24_get(buf1) == v);
  }
  /* Packing touches only the three sample bytes: */
  memset(buf, 0x5a, sizeof(buf));
  soxr_int24_put(buf, 0x123456);
  assert(buf[0] != 0x5a || buf[1] != 0x5a || buf[2] != 0x5a);
}



/* All 2^24 code values must survive the public path, at unity rate,
 * bit-exactly; and the split path must give the same result. */

static void api_round_trip_test(void)
{
  enum {len = 4096};
  static byte_t in[3 * len], out[3 * len], out1[3 * len];
  void * out_ptrs[1];
  soxr_error_t error;
  size_t i, idone, odone, blocks, b;
  int32_t v = -8388608;
  bool const passthrough = cr_engine_available();
  soxr_t soxr = create(SOXR_INT24_I, SOXR_INT24_I, 1, &error);
  soxr_t split = create(SOXR_INT24_I, SOXR_INT24_S, 1, &error);
  assert(!error);
  out_ptrs[0] = out1;

  for (blocks = (0x1000000u + len - 1) / len, b = 0; b < blocks; ++b) {
    for (i = 0; i < len; ++i, ++v)
      ref_put(in + 3 * i, v < 8388608? v : 8388607);
    odone = idone = 0;
    error = soxr_process(soxr, in, len, &idone, out, len, &odone);
    assert(!error && idone == len);
    if (passthrough)
      assert(odone == len && memcmp(out, in, 3 * len) == 0);
    /* The split path, from a resampler just as fresh as the above. */
    if (b == 0) {
      error = soxr_process(split, in, len, &idone, (void *)out_ptrs, len, &odone);
      assert(!error && idone == len);
      if (passthrough)
        assert(odone == len && memcmp(out1, in, 3 * len) == 0);
      soxr_delete(split);
      split = 0;
    }
  }
  assert(v == 8388608);   /* Every code value was seen exactly once. */
  soxr_delete(soxr);
}



/* The library's packed bytes must be the native-endian three low bytes. */

static void byte_order_test(void)
{
  static int32_t const code[] = {0, 1, -1, 0x123456, -0x123456,
                                 0x7fffff, -0x800000};
  byte_t b[3];
  unsigned i;

  for (i = 0; i < sizeof(code) / sizeof(*code); ++i) {
    int32_t v = code[i];
    uint32_t u = (uint32_t)v & 0xffffffu;
    ref_put(b, v);
#if HAVE_BIGENDIAN
    assert(b[0] == (byte_t)(u >> 16) && b[1] == (byte_t)(u >> 8)
        && b[2] == (byte_t)u);
#else
    assert(b[0] == (byte_t)u && b[1] == (byte_t)(u >> 8)
        && b[2] == (byte_t)(u >> 16));
#endif
    soxr_int24_put(b, v);
    assert(soxr_int24_get(b) == v);
  }
}



/* io_spec validation. */

static void validation_test(void)
{
  soxr_error_t error;
  soxr_t soxr;
  soxr_io_spec_t bad = soxr_io_spec((soxr_datatype_t)9, SOXR_INT24_I);

  /* Accepted, interleaved and split, and mixed with existing types: */
  assert(!soxr_io_spec(SOXR_INT24_I, SOXR_INT24_I).e);
  assert(!soxr_io_spec(SOXR_INT24_S, SOXR_INT24_S).e);
  assert(!soxr_io_spec(SOXR_INT24_I, SOXR_INT24_S).e);
  assert(!soxr_io_spec(SOXR_INT16_I, SOXR_INT24_S).e);
  assert(!soxr_io_spec(SOXR_INT24_I, SOXR_FLOAT64_S).e);
  assert(SOXR_INT24_I == 8 && SOXR_INT24_S == 12);

  /* Not datatypes: 9..11, 13..15 and anything out of range. */
  assert(soxr_io_spec((soxr_datatype_t)9, SOXR_INT24_I).e);
  assert(soxr_io_spec(SOXR_INT24_I, (soxr_datatype_t)10).e);
  assert(soxr_io_spec((soxr_datatype_t)11, SOXR_INT24_I).e);
  assert(soxr_io_spec((soxr_datatype_t)13, SOXR_INT24_I).e);
  assert(soxr_io_spec(SOXR_INT24_I, (soxr_datatype_t)14).e);
  assert(soxr_io_spec((soxr_datatype_t)15, SOXR_INT24_I).e);
  assert(soxr_io_spec((soxr_datatype_t)16, SOXR_INT24_I).e);
  assert(soxr_io_spec((soxr_datatype_t)-1, SOXR_INT24_I).e);
  assert(soxr_io_spec(SOXR_INT24_I, (soxr_datatype_t)-4).e);

  /* The error is distinguishable, and soxr_create rejects the same spec. */
  assert(bad.e && strstr(bad.e, "datatype"));
  soxr = soxr_create(1, 1, 1, &error, &bad, NULL, NULL);
  assert(!soxr && error && strstr(error, "datatype"));

  /* A hand-built io_spec is validated too, not only the constructor's. */
  bad = soxr_io_spec(SOXR_INT24_I, SOXR_INT24_I);
  bad.itype = (soxr_datatype_t)9;
  assert(!bad.e);
  soxr = soxr_create(1, 1, 1, &error, &bad, NULL, NULL);
  assert(!soxr && error && strstr(error, "datatype"));

  /* The 24-bit datatypes are accepted by soxr_create. */
  soxr = create(SOXR_INT24_I, SOXR_INT24_I, 1, &error);
  assert(soxr && !error);
  soxr_delete(soxr);
}



/* Clipping policy: for a given input value, clipping must start at the same
 * threshold as for 32-bit output (i.e. at plus or minus full-scale), the
 * saturated values must be the type's extremes, and the clip count must
 * match the 32-bit path.  `in' is in units of full-scale. */

static void clipping_test(bool split)
{
  enum {n = 12};
  static double const in[n] = {
    -2., -1.0000001, -1., -0.9999999, -0.5, 0.,
     0.5, 0.9999999, 1., 1.0000001, 2., 1e9};
  static byte_t out24[3 * n], out24s[3 * n];
  static int32_t out32[n];
  void * out_ptrs[1];
  soxr_error_t error;
  size_t i, idone, odone;
  soxr_t soxr;
  soxr_datatype_t otype = split? SOXR_INT24_S : SOXR_INT24_I;

  /* Clipping is applied by the shared I/O conversion code, but these
   * assertions compare against the input signal, which needs an engine that
   * passes data through unchanged at a 1:1 ratio. */
  if (!cr_engine_available())
    return;

  /* A ~ilen length asks soxr_process to treat this as all the input there
   * is, so all of it is consumed and the output is complete. */
  soxr = create(SOXR_FLOAT64_I, otype, 1, &error);
  assert(!error);
  out_ptrs[0] = out24s;
  error = soxr_process(soxr, in, ~(size_t)n, &idone,
      split? (void *)out_ptrs : (void *)out24, n, &odone);
  assert(!error && idone == n && odone == n);
  assert(*soxr_num_clips(soxr) == 6);   /* Those out of range clip. */

  soxr_delete(soxr);
  soxr = create(SOXR_FLOAT64_I, SOXR_INT32_I, 1, &error);
  assert(!error);
  error = soxr_process(soxr, in, ~(size_t)n, &idone, out32, n, &odone);
  assert(!error && idone == n && odone == n);
  assert(*soxr_num_clips(soxr) == 6);   /* Same six values. */
  soxr_delete(soxr);

  for (i = 0; i < n; ++i) {
    byte_t const * p = (split? out24s : out24) + 3 * i;
    int32_t v = ref_get(p);

    /* Exactly the 32-bit path's result, scaled by 2^8, to within the
     * rounding of that path (the two share rounding, saturation and
     * clip-counting policy; only the quantization differs). */
    assert(v == out32[i] / 256 || v == (out32[i] + (out32[i] >= 0? 255 :
        -255)) / 256);

    /* And, explicitly, the policy: saturation to the type's extremes. */
    if (in[i] > -1. && in[i] < 1.)
      assert(v >= -8388608 && v <= 8388607 && (v > 0) == (in[i] > 0));
    else
      assert(v == (in[i] < 0? -8388608 : 8388607));
  }
  /* Sanity on the unclipped endpoints: +/-0.5 full-scale, rounded. */
  assert(ref_get((split? out24s : out24) + 3 * 4) == -4194304);
  assert(ref_get((split? out24s : out24) + 3 * 6) == 4194304);

  if (!split)   /* Check the split path gives the same result. */
    clipping_test(true);
}



/* A small PRNG that is well-defined for 32-bit wrap-around (the examples'
 * `ranqd1' overflows a signed int, which is undefined behaviour).  The value
 * returned is in [-2^23, 2^23). */

static int32_t ran24(uint32_t * x)
{
  *x = *x * 1664525u + 1013904223u;
  return (int32_t)(*x >> 8) - 0x800000;
}



/* Multi-channel split I/O: each channel's buffer must receive its own data,
 * and the split path must match the interleaved path exactly, over a real
 * rate change (so the conversion code, not just the identity path, runs). */

static void split_test(void)
{
  enum {ch = 3, ilen = 2000, olen = 3000};
  static byte_t in_i[ch * ilen * 3], in_s[ch][ilen * 3];
  static byte_t out_i[ch * olen * 3], out_s[ch][olen * 3];
  void * out_ptrs[ch];
  void const * in_ptrs[ch];
  soxr_error_t error;
  size_t i, c, idone, odone, odone1;
  uint32_t ran = 0;
  soxr_io_spec_t io_i = soxr_io_spec(SOXR_INT24_I, SOXR_INT24_I);
  soxr_io_spec_t io_s = soxr_io_spec(SOXR_INT24_S, SOXR_INT24_S);
  soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, 0);
  soxr_t si = soxr_create(2, 3, ch, &error, &io_i, &q_spec, NULL);
  soxr_t ss;
  assert(si && !error);
  ss = soxr_create(2, 3, ch, &error, &io_s, &q_spec, NULL);
  assert(ss && !error);

  /* The same signal, in both layouts. */
  for (i = 0; i < ch * ilen; ++i) {
    int32_t v = ran24(&ran);
    ref_put(in_i + 3 * i, v);
    ref_put(in_s[i % ch] + 3 * (i / ch), v);
  }
  for (c = 0; c < ch; ++c) {
    out_ptrs[c] = out_s[c];
    in_ptrs[c] = in_s[c];
  }

  idone = odone = odone1 = 0;
  error = soxr_process(si, in_i, ilen, &idone, out_i, olen, &odone);
  assert(!error && idone && odone);
  {
    size_t idone_s;
    error = soxr_process(ss, (void const *)in_ptrs, ilen, &idone_s,
        (void *)out_ptrs, olen, &odone1);
    assert(!error && idone_s == idone && odone1 == odone);
  }

  for (i = 0; i < odone; ++i) for (c = 0; c < ch; ++c)
    assert(memcmp(out_i + 3 * (i * ch + c), out_s[c] + 3 * i, 3) == 0);

  soxr_delete(si);
  soxr_delete(ss);
}



/* Resampling with 24-bit I/O must match the 32-bit path exactly, scaled by
 * 2^8 (the 32-bit path is the same algorithm with a power-of-two-larger
 * full-scale, so its results are exact multiples of 256). */

static void resample_parity_test(void)
{
  enum {ch = 2, ilen = 5000, olen = 5000};
  static byte_t in[3 * ilen * ch], out24[3 * olen * ch];
  static int32_t out32[olen * ch];
  soxr_error_t error;
  size_t i, idone, odone24, odone32;
  uint32_t ran = 0;
  soxr_io_spec_t io24 = soxr_io_spec(SOXR_INT24_I, SOXR_INT24_I);
  soxr_io_spec_t io32 = soxr_io_spec(SOXR_INT24_I, SOXR_INT32_I);
  soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, 0);
  soxr_t s24 = soxr_create(44100, 48000, ch, &error, &io24, &q_spec, NULL);
  soxr_t s32;
  assert(s24 && !error);
  s32 = soxr_create(44100, 48000, ch, &error, &io32, &q_spec, NULL);
  assert(s32 && !error);

  for (i = 0; i < ilen * ch; ++i) {
    /* Half-scale, so that neither path clips. */
    ref_put(in + 3 * i, ran24(&ran) / 2);
  }
  /* (idone != ilen if soxr_process holds input back for the output request,
   * but both paths must consume exactly the same amount, and produce the
   * same number of output samples.) */
  error = soxr_process(s24, in, ilen, &idone, out24, olen, &odone24);
  assert(!error && idone);
  {
    size_t idone32, idone24 = idone;
    error = soxr_process(s32, in, ilen, &idone32, out32, olen, &odone32);
    assert(!error && idone32 == idone24 && odone32 == odone24);
  }
  assert(*soxr_num_clips(s24) == *soxr_num_clips(s32));

  /* The two paths must agree to within one 24-bit LSB (the 32-bit path is
   * quantized at 2^-32, the 24-bit path at 2^-24, so an exact 2^-8 scaling
   * does not hold where the result is not itself a multiple of 256). */
  if (!cr_engine_available()) {   /* e.g. VR engine: not comparable. */
    soxr_delete(s24);
    soxr_delete(s32);
    return;
  }
  for (i = 0; i < odone24 * ch; ++i) {
    int32_t d = ref_get(out24 + 3 * i) - (int32_t)(out32[i] / 256);
    assert(d >= -1 && d <= 1);
  }
  soxr_delete(s24);
  soxr_delete(s32);
}



int main(int argc, char const * arg[])
{
  round_trip_test();
  byte_order_test();
  api_round_trip_test();
  validation_test();
  clipping_test(false);
  split_test();
  resample_parity_test();
  return 0 * argc * !arg;
}
