/*
 * rapi_posix_missing.h — declarations for the POSIX calls this port supplies
 * itself.
 *
 * The build hands this header to TyrQuake's operating-system layer, and only
 * to that file, before anything else it includes. That file is the one place
 * in the game that talks to the operating system directly, and it is the one
 * place that reaches a function the C library on this board neither declares
 * nor defines.
 *
 * Today that is one function. Newlib's <time.h> puts nanosleep behind a
 * feature-test macro that this configuration of newlib never sets — the
 * block is written for Cygwin and for RTEMS, and this is neither — so no
 * amount of asking for POSIX from the command line will produce the
 * declaration. The function is implemented in this port's own
 * circle_stubs.cpp, on top of SDL's delay.
 *
 * Handing it over this way, rather than editing the game, is what keeps the
 * upstream submodule untouched.
 */
#ifndef _rapi_posix_missing_h
#define _rapi_posix_missing_h

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

#ifdef __cplusplus
}
#endif

#endif
