/* SoX Resampler Library      Copyright (c) 2007-18 robs@users.sourceforge.net
 * Licence for this file: LGPL v2.1                  See LICENCE for details. */

/* Test: soxr_output_length predicts exactly the number of output samples
 * (incl. filter delay & flush tail), for rational & irrational ratios, all
 * quality levels, and variable-rate; also overflow & error-state handling. */

#ifdef NDEBUG /* N.B. assert used with active statements so enable always. */
#undef NDEBUG /* Must undef above assert.h or other that might include it. */
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <soxr.h>

#define AL(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ((a) < (b)? (a) : (b))

static int32_t ran;
#define ranqd1(x) ((x) = 1664525 * (x) + 1013904223) /* int32_t x */
#define franqd1(x) (float)(ranqd1(x) * (1. / (65536. * 32768.))) /* [-1,1) */



/* Flush and drain the resampler, checking at each step that the queried
 * remaining output length stays consistent with `baseline'. */
static size_t drain(soxr_t soxr, float * obuf, size_t produced, size_t ocap,
    unsigned ch, size_t baseline)
{
  size_t odone, rem;
  soxr_error_t error;
  int eoi_checked = 0;
  do {
    size_t olen = min((size_t)1024, ocap - produced);
    error = soxr_process(soxr, 0, 0, 0, obuf + produced * ch, olen, &odone);
    assert(!error);
    produced += odone;
    assert(produced <= baseline);
    error = soxr_output_length(soxr, 0, &rem);
    assert(!error);
    assert(produced + rem == baseline);
    if (!eoi_checked) { /* Further input must be rejected once flushing: */
      rem = 777;
      error = soxr_output_length(soxr, 1, &rem);
      assert(error && !strcmp(error, "end-of-input already indicated"));
      assert(rem == 777); /* olen unmodified on error */
      eoi_checked = 1;
    }
  } while (odone);
  return produced;
}



/* Stream `ilen' samples (per channel) through `soxr' in random-sized chunks,
 * verifying that soxr_output_length exactly predicts the remaining output at
 * every step. */
static void run_case(double irate, double orate, unsigned long recipe,
    unsigned long flags, unsigned ch, size_t ilen)
{
  soxr_error_t error;
  soxr_quality_spec_t q_spec = soxr_quality_spec(recipe, flags);
  soxr_t soxr = soxr_create(irate, orate, ch, &error, 0, &q_spec, 0);
  float * ibuf, * obuf;
  size_t i, consumed = 0, produced = 0, baseline, rem, ocap;

  assert(soxr && !error);
  ibuf = (float *)malloc(ilen * ch * sizeof(*ibuf));
  assert(ibuf);
  for (i = 0; i < ilen * ch; ++i)
    ibuf[i] = franqd1(ran);

  error = soxr_output_length(soxr, ilen, &baseline);
  assert(!error);
  ocap = baseline + 512; /* Slack, so a wrong prediction can't clip. */
  obuf = (float *)malloc(ocap * ch * sizeof(*obuf));
  assert(obuf);

  while (consumed < ilen) {
    size_t n = 1 + (size_t)(ranqd1(ran) & 0x1ff); /* 1..512 */
    size_t olen, odone;
    if (n > ilen - consumed)
      n = ilen - consumed;
    consumed += n;
    olen = min(1 + (size_t)(ranqd1(ran) & 0x3ff), ocap - produced);
    error = soxr_process(soxr, ibuf + (consumed - n) * ch, n, 0,
        obuf + produced * ch, olen, &odone);
    assert(!error);
    produced += odone;
    assert(produced <= baseline);
    error = soxr_output_length(soxr, ilen - consumed, &rem);
    assert(!error);
    assert(produced + rem == baseline);
  }
  error = soxr_output_length(soxr, 0, &rem);
  assert(!error);
  assert(produced + rem == baseline);
  produced = drain(soxr, obuf, produced, ocap, ch, baseline);
  assert(produced == baseline); /* Prediction == actual, exactly. */

  free(obuf);
  free(ibuf);
  soxr_delete(soxr);
  fprintf(stderr, ".");
}



/* Variable-rate with mid-stream ratio changes: the engine's output count is
 * then sensitive to input blocking, so the exact guarantee applies to the
 * canonical continuation: query, then supply all remaining input in a single
 * block, and the prediction must match the actual output exactly. */
static void run_case_vr_changes(double irate, double orate, size_t ilen,
    double const * ratios, size_t const * change_at, size_t num_changes)
{
  soxr_error_t error;
  soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, SOXR_VR);
  soxr_t soxr = soxr_create(irate, orate, 1, &error, 0, &q_spec, 0);
  float * ibuf, * obuf;
  size_t i, consumed = 0, produced = 0, at_query, rem, ocap, next_change = 0;
  size_t odone;

  assert(soxr && !error);
  ibuf = (float *)malloc(ilen * sizeof(*ibuf));
  assert(ibuf);
  for (i = 0; i < ilen; ++i)
    ibuf[i] = franqd1(ran);
  ocap = 4 * ilen + 65536;
  obuf = (float *)malloc(ocap * sizeof(*obuf));
  assert(obuf);

  while (consumed < change_at[num_changes - 1] + 2000) {
    size_t n = 1 + (size_t)(ranqd1(ran) & 0x1ff); /* 1..512 */
    size_t olen;
    consumed += n;
    if (next_change < num_changes && consumed >= change_at[next_change]) {
      error = soxr_set_io_ratio(soxr, ratios[next_change],
          (next_change & 1)? 2000 : 0); /* Exercise slewed changes too. */
      assert(!error);
      ++next_change;
    }
    olen = min(1 + (size_t)(ranqd1(ran) & 0x3ff), ocap - produced);
    error = soxr_process(soxr, ibuf + (consumed - n), n, 0,
        obuf + produced, olen, &odone);
    assert(!error);
    produced += odone;
  }
  error = soxr_output_length(soxr, ilen - consumed, &rem);
  assert(!error);
  at_query = produced;

  /* Canonical finish: all remaining input in a single block: */
  error = soxr_process(soxr, ibuf + consumed, ilen - consumed, 0,
      obuf + produced, ocap - produced, &odone);
  assert(!error);
  produced += odone;
  do {
    error = soxr_process(soxr, 0, 0, 0, obuf + produced,
        min((size_t)65536, ocap - produced), &odone);
    assert(!error);
    produced += odone;
  } while (odone);
  assert(produced - at_query == rem); /* Prediction == actual, exactly. */

  free(obuf);
  free(ibuf);
  soxr_delete(soxr);
  fprintf(stderr, ".");
}



static void check_errors(void)
{
  size_t n;
  soxr_error_t error;
  soxr_t soxr;
  char const *e_null, *e_overflow, *e_unset, *e_eoi, *e_state;
  float obuf[8];
  size_t odone;

  n = 12345;
  e_null = soxr_output_length(0, 0, &n);
  assert(e_null && !strcmp(e_null, "null pointer") && n == 12345);

  soxr = soxr_create(44100, 48000, 1, &error, 0, 0, 0);
  assert(soxr && !error);
  error = soxr_output_length(soxr, 10, 0);
  assert(error && !strcmp(error, "null pointer"));
  error = soxr_output_length(soxr, 0, &n); /* Nothing in, nothing out. */
  assert(!error && n == 0);
  soxr_delete(soxr);

  soxr = soxr_create(44100, 44100, 1, &error, 0, 0, 0); /* Unity ratio. */
  assert(soxr && !error);
  n = 12345;
  e_overflow = soxr_output_length(soxr, (size_t)-1, &n);
  assert(e_overflow && !strcmp(e_overflow, "output length too large"));
  assert(n == 12345);
  if (sizeof(size_t) >= 8) { /* Just within range must succeed: */
    error = soxr_output_length(soxr, (size_t)1 << 62, &n);
    assert(!error && n == (size_t)1 << 62);
  }
  soxr_delete(soxr);

  soxr = soxr_create(0, 0, 1, &error, 0, 0, 0); /* No io_ratio yet. */
  assert(soxr && !error);
  n = 12345;
  e_unset = soxr_output_length(soxr, 10, &n);
  assert(e_unset && !strcmp(e_unset, "O/I ratio not yet set") && n == 12345);
  error = soxr_set_io_ratio(soxr, 1.5, 0);
  assert(!error);
  error = soxr_output_length(soxr, 10, &n);
  assert(!error && n == 7); /* round(10 / 1.5) */
  soxr_delete(soxr);

  soxr = soxr_create(44100, 48000, 1, &error, 0, 0, 0);
  assert(soxr && !error);
  error = soxr_process(soxr, 0, 0, 0, obuf, AL(obuf), &odone); /* Flush. */
  assert(!error);
  n = 12345;
  e_eoi = soxr_output_length(soxr, 1, &n);
  assert(e_eoi && !strcmp(e_eoi, "end-of-input already indicated"));
  assert(n == 12345);
  error = soxr_output_length(soxr, 0, &n); /* Remaining tail is queryable. */
  assert(!error);
  soxr_delete(soxr);

  soxr = soxr_create(44100, 48000, 1, &error, 0, 0, 0);
  assert(soxr && !error);
  assert(!soxr_output(soxr, 0, 5)); /* Induce an error state. */
  e_state = soxr_output_length(soxr, 0, &n);
  assert(e_state && !strcmp(e_state, soxr_error(soxr)));
  soxr_delete(soxr);

  /* All error states distinguishable: */
  assert(strcmp(e_null, e_overflow) && strcmp(e_null, e_unset) &&
      strcmp(e_null, e_eoi) && strcmp(e_null, e_state) &&
      strcmp(e_overflow, e_unset) && strcmp(e_overflow, e_eoi) &&
      strcmp(e_overflow, e_state) && strcmp(e_unset, e_eoi) &&
      strcmp(e_unset, e_state) && strcmp(e_eoi, e_state));
}



int main(void)
{
  double const ratios[] = {.5, 1.7};
  size_t const change_at[] = {10000, 20000};

  check_errors();

  /* Constant-rate engines (cubic, cr32/cr64 & SIMD variants), various
   * ratios (rational, irrational, unity, up & down), phases & flags: */
  run_case(44100, 48000, SOXR_QQ, 0, 1, 30000);
  run_case(48000, 44100, SOXR_LQ, 0, 1, 30000);
  run_case(44100, 65537, SOXR_MQ, 0, 1, 30000);
  run_case(44100, 48000, SOXR_HQ, 0, 1, 50000);
  run_case(48000, 44100, SOXR_HQ, 0, 1, 50000);
  run_case(192000, 44100, SOXR_HQ, 0, 1, 50000);
  run_case(44100, 192000, SOXR_HQ, 0, 1, 30000);
  run_case(44100, 44100, SOXR_HQ, 0, 1, 30000);
  run_case(44100, 48000, SOXR_VHQ, 0, 1, 30000);
  run_case(44100, 48000, SOXR_HQ, SOXR_HI_PREC_CLOCK, 1, 30000);
  run_case(44100, 48000, SOXR_HQ | SOXR_MINIMUM_PHASE, 0, 1, 30000);
  run_case(48000, 44100, SOXR_HQ | SOXR_STEEP_FILTER, 0, 1, 30000);
  run_case(44100, 48000, SOXR_HQ, 0, 2, 30000); /* Multi-channel. */

  /* Variable-rate engine, constant ratio (exact for any blocking): */
  run_case(44100, 48000, SOXR_HQ, SOXR_VR, 1, 30000);
  run_case(48000, 44100, SOXR_HQ, SOXR_VR, 1, 30000);

  /* Variable-rate engine with mid-stream ratio changes (canonical finish): */
  run_case_vr_changes(44100, 48000, 30000, ratios, change_at, AL(ratios));

  fprintf(stderr, "\nok\n");
  return 0;
}
