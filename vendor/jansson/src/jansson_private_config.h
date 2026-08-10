/* Static private configuration for the vendored Jansson build. */

#ifndef JANSSON_PRIVATE_CONFIG_H
#define JANSSON_PRIVATE_CONFIG_H

#include <stdint.h>

#define HAVE_STDINT_H 1
#define HAVE_LOCALE_H 1
#define HAVE_SETLOCALE 1

#define HAVE_INT32_T 1
#define HAVE_UINT32_T 1
#define HAVE_UINT16_T 1
#define HAVE_UINT8_T 1

#if defined(_WIN32)
#ifndef ssize_t
#define ssize_t intptr_t
#endif

#define USE_WINDOWS_CRYPTOAPI 1
#else
#include <sys/types.h>

#define HAVE_FCNTL_H 1
#define HAVE_SCHED_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_ENDIAN_H 1

#define HAVE_OPEN 1
#define HAVE_CLOSE 1
#define HAVE_READ 1
#define HAVE_GETPID 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_SCHED_YIELD 1
#define HAVE_ATOMIC_BUILTINS 1

#define USE_URANDOM 1
#endif

#define USE_DTOA 1
#define DTOA_ENABLED 1

#define INITIAL_HASHTABLE_ORDER 3

#endif
