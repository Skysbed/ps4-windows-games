#ifndef PCPS4_STATUS_H
#define PCPS4_STATUS_H

/*
 * PcPs4 on-console status reporting.
 *
 * Gives visible feedback without a PC attached:
 *   - a status screen on the TV: one bar per test, green = passed, red =
 *     failed, with a top banner that turns fully green or red at the end;
 *   - a log file (/data/pcps4_log.txt) written and flushed line by line, so
 *     if the app crashes the last line shows exactly which step it reached;
 *   - the same lines on stdout (klog), unbuffered.
 *
 * pcps4_status_finish_forever() keeps the app alive on the result screen
 * instead of returning from main, matching OpenOrbis sample behaviour.
 */

#ifdef __cplusplus
extern "C" {
#endif

void pcps4_status_begin(void);
void pcps4_status_log(const char *fmt, ...);
void pcps4_status_step(const char *name);          /* "about to run X" */
void pcps4_status_report(const char *name, int ok); /* record a result */
void pcps4_status_finish_forever(void);            /* never returns */

#ifdef __cplusplus
}
#endif

#endif
