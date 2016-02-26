/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FV_UTIL_H
#define FV_UTIL_H

#include <stdlib.h>
#include <stdint.h>

#define FV_SWAP_UINT16(x)                       \
  ((uint16_t)                                   \
   (((uint16_t) (x) >> 8) |                     \
    ((uint16_t) (x) << 8)))
#define FV_SWAP_UINT32(x)                               \
  ((uint32_t)                                           \
   ((((uint32_t) (x) & UINT32_C (0x000000ff)) << 24) |  \
    (((uint32_t) (x) & UINT32_C (0x0000ff00)) << 8) |   \
    (((uint32_t) (x) & UINT32_C (0x00ff0000)) >> 8) |   \
    (((uint32_t) (x) & UINT32_C (0xff000000)) >> 24)))
#define FV_SWAP_UINT64(x)                                               \
  ((uint64_t)                                                           \
   ((((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000000000ff)) << 56) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000000000ff00)) << 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000000000ff0000)) << 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000ff000000)) << 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000ff00000000)) >> 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000ff0000000000)) >> 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00ff000000000000)) >> 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0xff00000000000000)) >> 56)))

#if defined(HAVE_BIG_ENDIAN)
#define FV_UINT16_FROM_BE(x) (x)
#define FV_UINT32_FROM_BE(x) (x)
#define FV_UINT64_FROM_BE(x) (x)
#define FV_UINT16_FROM_LE(x) FV_SWAP_UINT16 (x)
#define FV_UINT32_FROM_LE(x) FV_SWAP_UINT32 (x)
#define FV_UINT64_FROM_LE(x) FV_SWAP_UINT64 (x)
#elif defined(HAVE_LITTLE_ENDIAN)
#define FV_UINT16_FROM_LE(x) (x)
#define FV_UINT32_FROM_LE(x) (x)
#define FV_UINT64_FROM_LE(x) (x)
#define FV_UINT16_FROM_BE(x) FV_SWAP_UINT16 (x)
#define FV_UINT32_FROM_BE(x) FV_SWAP_UINT32 (x)
#define FV_UINT64_FROM_BE(x) FV_SWAP_UINT64 (x)
#else
#error Platform is neither little-endian nor big-endian
#endif

#define FV_UINT16_TO_LE(x) FV_UINT16_FROM_LE (x)
#define FV_UINT16_TO_BE(x) FV_UINT16_FROM_BE (x)
#define FV_UINT32_TO_LE(x) FV_UINT32_FROM_LE (x)
#define FV_UINT32_TO_BE(x) FV_UINT32_FROM_BE (x)
#define FV_UINT64_TO_LE(x) FV_UINT64_FROM_LE (x)
#define FV_UINT64_TO_BE(x) FV_UINT64_FROM_BE (x)

#define FV_STMT_START do
#define FV_STMT_END while (0)

#define FV_N_ELEMENTS(array)                    \
  (sizeof (array) / sizeof ((array)[0]))

#define FV_STRINGIFY(macro_or_string) FV_STRINGIFY_ARG (macro_or_string)
#define FV_STRINGIFY_ARG(contents) #contents

#ifdef __GNUC__
#define FV_NO_RETURN __attribute__((noreturn))
#define FV_PRINTF_FORMAT(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#define FV_NULL_TERMINATED __attribute__((sentinel))
#else
#define FV_NO_RETURN
#define FV_PRINTF_FORMAT(string_index, first_to_check)
#define FV_NULL_TERMINATED
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define FV_STMT_START do
#define FV_STMT_END while (0)

#define fv_return_if_fail(condition)                    \
  FV_STMT_START {                                       \
          if (!(condition)) {                           \
                  fv_warning("assertion '%s' failed",   \
                             #condition);               \
                  return;                               \
          }                                             \
  } FV_STMT_END

#define fv_return_val_if_fail(condition, val)                   \
        FV_STMT_START {                                         \
                if (!(condition)) {                             \
                        fv_warning("assertion '%s' failed",     \
                                   #condition);                 \
                        return (val);                           \
                }                                               \
        } FV_STMT_END

#define fv_warn_if_reached()                                            \
        FV_STMT_START {                                                 \
                fv_warning("Line %i in %s should not be reached",       \
                           __LINE__,                                    \
                           __FILE__);                                   \
        } FV_STMT_END

FV_NO_RETURN
FV_PRINTF_FORMAT(1, 2)
void
fv_fatal(const char *format, ...);

FV_PRINTF_FORMAT(1, 2)
void
fv_warning(const char *format, ...);

void *
fv_alloc(size_t size);

void *
fv_calloc(size_t size);

void *
fv_realloc(void *ptr, size_t size);

char *
fv_strdup(const char *str);

void *
fv_memdup(const void *data, size_t size);

FV_NULL_TERMINATED char *
fv_strconcat(const char *string1, ...);

void
fv_free(void *ptr);

#ifdef HAVE_FFS
#include <strings.h>
#define fv_util_ffs(x) ffs(x)
#else
int
fv_util_ffs(int value);
#endif

#ifdef HAVE_FFSL
#include <string.h>
#define fv_util_ffsl(x) ffsl(x)
#else
int
fv_util_ffsl(long int value);
#endif

#ifdef WIN32
#define FV_PATH_SEPARATOR "\\"
#else
#define FV_PATH_SEPARATOR "/"
#endif

/**
 * Align a value, only works pot alignemnts.
 */
static inline int
fv_align(int value, int alignment)
{
   return (value + alignment - 1) & ~(alignment - 1);
}

#endif /* FV_UTIL_H */
