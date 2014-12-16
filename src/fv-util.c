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

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include "fv-util.h"

void
fv_fatal(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);

        fputc('\n', stderr);

        fflush(stderr);

        abort();
}

void
fv_warning(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);

        fputc('\n', stderr);
}

void *
fv_alloc(size_t size)
{
        void *result = malloc(size);

        if (result == NULL)
                fv_fatal("Memory exhausted");

        return result;
}

void *
fv_calloc(size_t size)
{
        void *result = fv_alloc(size);

        memset(result, 0, size);

        return result;
}

void *
fv_realloc(void *ptr, size_t size)
{
        if (ptr == NULL)
                return fv_alloc(size);

        ptr = realloc(ptr, size);

        if (ptr == NULL)
                fv_fatal("Memory exhausted");

        return ptr;
}

char *
fv_strdup(const char *str)
{
        return fv_memdup(str, strlen(str) + 1);
}

void *
fv_memdup(const void *data, size_t size)
{
        void *ret;

        ret = fv_alloc(size);
        memcpy(ret, data, size);

        return ret;
}

char *
fv_strconcat(const char *string1, ...)
{
        size_t string1_length;
        size_t total_length;
        size_t str_length;
        va_list ap, apcopy;
        const char *str;
        char *result, *p;

        if (string1 == NULL)
                return fv_strdup("");

        total_length = string1_length = strlen(string1);

        va_start(ap, string1);
        va_copy(apcopy, ap);

        while ((str = va_arg(ap, const char *)))
                total_length += strlen(str);

        va_end(ap);

        result = fv_alloc(total_length + 1);
        memcpy(result, string1, string1_length);
        p = result + string1_length;

        while ((str = va_arg(apcopy, const char *))) {
                str_length = strlen(str);
                memcpy(p, str, str_length);
                p += str_length;
        }
        *p = '\0';

        va_end(apcopy);

        return result;
}

void
fv_free(void *ptr)
{
        if (ptr)
                free(ptr);
}
