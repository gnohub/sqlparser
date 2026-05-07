/*
 * Static private configuration for the vendored Jansson MSVC build.
 */

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

#ifndef ssize_t
#define ssize_t intptr_t
#endif

#define USE_WINDOWS_CRYPTOAPI 1
#define USE_DTOA 1
#define DTOA_ENABLED 1

#define INITIAL_HASHTABLE_ORDER 3

#endif
