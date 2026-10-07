/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_CAPTURE_H
#define RUNTIME_CAPTURE_H

/* Recording inputs in a real run for tools/verify.py (docs/VERIFY.md). Off unless
   BB_CAPTURE_DIR is set. Every hooked function that records writes to
   $BB_CAPTURE_DIR/<function>/<run>_NNNN.json, outside the repository, so one run feeds every
   function and later runs add to the same library (<run>: BB_CAPTURE_RUN, or start time and
   process id). Per function and run: the first 50 calls, then every 1000th, at most 200. */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the case number to record for this call, or -1 when nothing is recorded. */
int rt_capture_begin(const char *function);
/* Writes the case file; `json` is the complete case object. */
void rt_capture_write(const char *function, int case_number, const char *json);

#ifdef __cplusplus
}
#endif

#endif
