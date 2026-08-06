/*
 * sys/ipc.h — present so an include succeeds, and empty because it must be.
 *
 * TyrQuake's operating-system layer includes this header near the top of the
 * file, along with sys/mman.h, and then uses nothing from either: the two
 * functions that would have needed them, Sys_MakeCodeWriteable and
 * Sys_MakeCodeUnwriteable, have bodies only in the x86 assembly build, which
 * this port does not select.
 *
 * The C library on this board has no System V inter-process communication,
 * because there are no processes: one program owns the machine. So the
 * header does not exist, the include fails, and the file that needs nothing
 * from it cannot be compiled.
 *
 * This is that include, satisfied. It declares nothing, so anything that
 * genuinely wanted a message queue or a shared memory segment still fails to
 * compile — which is the right answer here.
 */
#ifndef _rapi_sys_ipc_h
#define _rapi_sys_ipc_h
#endif
