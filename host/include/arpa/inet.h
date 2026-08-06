/*
 * arpa/inet.h — the byte-order helpers, without the sockets.
 *
 * THIS HEADER EXISTS TO BE FOUND FIRST. TyrQuake's network layer includes
 * <arpa/inet.h> in one file, for one thing: htons(), to put a port number
 * into network byte order. Upstream says so in a comment above the include.
 *
 * The C library on this board ships an arpa/inet.h of its own, but it is a
 * declaration of the address-conversion functions and it is written in terms
 * of socklen_t — a type that only exists where sockets do, and there are no
 * sockets here. Including it stops the compilation on an unknown type,
 * before the one function the game actually wants.
 *
 * So this port supplies the header instead, on its own include path ahead of
 * the C library's. It gives the four byte-order functions, which are pure
 * arithmetic and need nothing underneath them, and it gives nothing else:
 * anything that genuinely wanted a socket fails to compile, which is the
 * right answer on a board that has no network stack.
 *
 * This is a build-time seam in this port's own layer. Nothing in the game's
 * source is changed, and the game reaches this header only because of where
 * the include path points.
 */
#ifndef _rapi_arpa_inet_h
#define _rapi_arpa_inet_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every Raspberry Pi runs little-endian, so network order — which is
   big-endian — is always a byte swap. Written as inline functions rather
   than macros so a caller passing an expression with a side effect gets it
   evaluated once. */
static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }

#ifdef __cplusplus
}
#endif

#endif
