/*
 * sys/mman.h — present so an include succeeds, and empty because it must be.
 *
 * The companion to sys/ipc.h beside it, included by the same file for the
 * same reason and used just as little. Memory mapping needs a memory manager
 * with page tables it can change on demand; this board runs one program in a
 * flat address space, and the C library offers no mmap or mprotect.
 *
 * Declaring nothing is deliberate. A caller that really wanted to map a file
 * or change a page's protection still fails to compile, rather than linking
 * against something that would quietly do nothing.
 */
#ifndef _rapi_sys_mman_h
#define _rapi_sys_mman_h
#endif
