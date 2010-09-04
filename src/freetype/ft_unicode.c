/*
  pygame - Python Game Library
  Copyright (C) 2009 Vicent Marti

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/* Pythons encoding is not quite what we want. Python UTF-16 and 32 encodings
 * pass through surrogate area codes untouched, and unchecked. So if one must
 * do ones own UTF-16 surrogate pair checking then one might as well perform
 * the entire translation.
 */

#define PYGAME_FREETYPE_INTERNAL
#include "ft_wrap.h"

#define SIZEOF_PGFT_STRING(len) \
    (sizeof(PGFT_String) + (ssize_t)(len) * sizeof(PGFT_char))

static const Py_UNICODE UNICODE_BOM_NATIVE = 0xFEFF;
static const Py_UNICODE UNICODE_BOM_SWAPPED = 0xFFFE;
static const Py_UNICODE UNICODE_HSA_START = 0xD800;
static const Py_UNICODE UNICODE_HSA_END = 0xD8FF;
static const Py_UNICODE UNICODE_LSA_START = 0xDC00;
static const Py_UNICODE UNICODE_LSA_END = 0xDFFF;
static const Py_UNICODE UNICODE_SA_START = 0xD800;
static const Py_UNICODE UNICODE_SA_END = 0xDFFF;

static void
Err_UnicodeEncodeError(const char *codec,
                       PyObject *unistr,
                       Py_ssize_t start,
                       Py_ssize_t end,
                       const char *reason);


PGFT_String *
PGFT_EncodePyString(PyObject *obj, int surrogates)
{
    PGFT_String *utf32_buffer = NULL;
    Py_ssize_t len;
    PGFT_char *dst;

    if (PyUnicode_Check(obj)) {
        const Py_UNICODE *src = PyUnicode_AS_UNICODE(obj);
        Py_UNICODE c;
        Py_ssize_t i, j, srclen;

        len = srclen = PyUnicode_GET_SIZE(obj);
        if (surrogates) {
            /* Do UTF-16 surrogate pair decoding. Calculate character count
             * and raise an exception if BOMs or malformed surrogate pairs
             * are found.
             */
            for (i = 0; i < srclen; ++i) {
                c = src[i];
                if (c == UNICODE_BOM_NATIVE || c == UNICODE_BOM_SWAPPED) {
                    Err_UnicodeEncodeError("utf-32", obj, i, i + 1,
                                           "no BOM handling");
                    return NULL;
                }
                if (c >= UNICODE_SA_START && c < UNICODE_SA_END) {
                    if (c > UNICODE_HSA_END) {
                        Err_UnicodeEncodeError(
                            "utf-32", obj, i, i + 1,
                            "missing high-surrogate code point");
                        return NULL;
                    }
                    if (++i == srclen) {
                        Err_UnicodeEncodeError(
                            "utf-32", obj, i - 1, i,
                            "missing low-surrogate code point");
                        return NULL;
                    }
                    c = src[i];
                    if (c < UNICODE_LSA_START || c > UNICODE_LSA_END) {
                        Err_UnicodeEncodeError(
                            "utf-32", obj, i, i + 1,
                            "expected low-surrogate code point");
                        return NULL;
                    }
                    --len;
                }
            }
        }       

        /* The Unicode is valid enough to use */
        utf32_buffer = (PGFT_String *)_PGFT_malloc(SIZEOF_PGFT_STRING(len));
        if (utf32_buffer == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        dst = utf32_buffer->data;
        if (surrogates) {
            for (i = 0, j = 0; i < srclen; ++i, ++j) {
                c = src[i];
                if (c >= UNICODE_HSA_START && c <= UNICODE_HSA_END)
                    c = ((c & 0x3FF) << 10 | (src[++i] & 0x3FF)) + 0x10000;
                dst[j] = (PGFT_char)c;
            }
        }
        else {
            for (i = 0; i < srclen; ++i) {
                dst[i] = (PGFT_char)src[i];
            }
        }
    }
    else if (Bytes_Check(obj)) {
        /*
         * For bytes objects, assume the bytes are
         * Latin1 text (who would manually enter bytes as
         * UTF8 anyway?), so manually copy the raw contents
         * of the object expanding each byte to 32 bits.
         */
        char *src;
        Py_ssize_t i;

        Bytes_AsStringAndSize(obj, &src, &len);
        utf32_buffer = (PGFT_String *)_PGFT_malloc(SIZEOF_PGFT_STRING(len));
        if (utf32_buffer == NULL) {
            PyErr_NoMemory();
            return NULL;
        }
        dst = utf32_buffer->data;
        for (i = 0; i < len; ++i) {
            dst[i] = (PGFT_char)(src[i]);
        }
    }
    else {
        PyErr_Format(PyExc_TypeError,
                     "Expected a Unicode or LATIN1 (bytes) string for text:"
                     " got type %.1024s", Py_TYPE(obj)->tp_name);
        return NULL;
    }

    utf32_buffer->data[len] = 0;
    utf32_buffer->length = len;
    return utf32_buffer;
}

void
Err_UnicodeEncodeError(const char *codec,
                       PyObject *unistr,
                       Py_ssize_t start,
                       Py_ssize_t end,
                       const char *reason)
{
    PyObject *e = PyObject_CallFunction(PyExc_UnicodeEncodeError, "sSkks",
                                        codec, unistr,
                                        (unsigned long)start,
                                        (unsigned long)end,
                                        reason);

    if (!e)
        return;
    Py_INCREF(PyExc_UnicodeEncodeError);
    PyErr_Restore(PyExc_UnicodeEncodeError, e, NULL);
}

