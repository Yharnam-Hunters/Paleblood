/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_CAPTURE_H
#define RUNTIME_CAPTURE_H

/* Recording inputs in a real run for tools/verify.py (docs/VERIFY.md). Off unless
   BB_CAPTURE_DIR is set. Every hooked function that records writes to
   $BB_CAPTURE_DIR/<function>/<run>_NNNN.json, outside the repository, so one run feeds every
   function and later runs add to the same library (<run>: BB_CAPTURE_RUN, or start time and
   process id). Per function and run: the first 50 calls, then every 1000th, until 200 distinct
   cases are kept (BB_CAPTURE_MAX). A case identical to one the library already holds for the
   function, apart from its id and run, is not written again (rt_capture_keep). */

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the case number to record for this call, or -1 when nothing is recorded. */
int rt_capture_begin(const char *function);
/* Whether BB_CAPTURE_ONLY (comma-separated names) lets this function record; unset: all. */
int rt_capture_wanted(const char *function);
/* Call before writing a case: 1 when it differs from every case the library and this run hold
   for the function (it is then counted towards BB_CAPTURE_MAX), 0 for a repeat to drop. */
int rt_capture_keep(const char *function, const char *json);
/* Stable identifier shared by cases in this capture process; NULL if capture is unavailable. */
const char *rt_capture_run_id(void);
/* Writes the case file; `json` is the complete case object. */
void rt_capture_write(const char *function, int case_number, const char *json);

#ifdef __cplusplus
}
#endif

#endif
