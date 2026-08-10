/* Static configuration for the vendored Jansson build. */

#ifndef JANSSON_CONFIG_H
#define JANSSON_CONFIG_H

#define JSON_INTEGER_IS_LONG_LONG 1

#define HAVE_STDINT_H 1
#include <stdint.h>

#define JSON_INLINE inline

#if defined(_MSC_VER)
#define JSON_HAVE_ATOMIC_BUILTINS 0
#else
#define JSON_HAVE_ATOMIC_BUILTINS 1
#endif
#define JSON_HAVE_SYNC_BUILTINS 0

#define JSON_PARSER_MAX_DEPTH 2048

#endif
