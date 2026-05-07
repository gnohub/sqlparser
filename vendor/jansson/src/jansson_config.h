/*
 * Static configuration for the vendored Jansson build used by the MSVC
 * Windows build. Linux builds continue to use the system-provided Jansson
 * headers through pkg-config.
 */

#ifndef JANSSON_CONFIG_H
#define JANSSON_CONFIG_H

#ifndef JANSSON_USING_CMAKE
#define JANSSON_USING_CMAKE
#endif

#define JSON_INTEGER_IS_LONG_LONG 1

#define HAVE_STDINT_H 1
#include <stdint.h>

#ifdef __cplusplus
#define JSON_INLINE inline
#else
#define JSON_INLINE inline
#endif

#define json_int_t long long
#define json_strtoint _strtoi64
#define JSON_INTEGER_FORMAT "I64d"

#define JSON_HAVE_ATOMIC_BUILTINS 0
#define JSON_HAVE_SYNC_BUILTINS 0

#define JSON_PARSER_MAX_DEPTH 2048

#endif
