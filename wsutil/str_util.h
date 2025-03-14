/* str_util.h
 * String utility definitions
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __STR_UTIL_H__
#define __STR_UTIL_H__

#include <wireshark.h>
#include <wsutil/wmem/wmem.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Convert all upper-case ASCII letters to their ASCII lower-case
 *  equivalents, in place, with a simple non-locale-dependent
 *  ASCII mapping (A-Z -> a-z).
 *  All other characters are left unchanged, as the mapping to
 *  lower case may be locale-dependent.
 *
 *  The string is assumed to be in a character encoding, such as
 *  an ISO 8859 or other EUC encoding, or UTF-8, in which all
 *  bytes in the range 0x00 through 0x7F are ASCII characters and
 *  non-ASCII characters are constructed from one or more bytes in
 *  the range 0x80 through 0xFF.
 *
 * @param str The string to be lower-cased.
 * @return    ptr to the string
 */
WS_DLL_PUBLIC
gchar *ascii_strdown_inplace(gchar *str);

/** Convert all lower-case ASCII letters to their ASCII upper-case
 *  equivalents, in place, with a simple non-locale-dependent
 *  ASCII mapping (a-z -> A-Z).
 *  All other characters are left unchanged, as the mapping to
 *  lower case may be locale-dependent.
 *
 *  The string is assumed to be in a character encoding, such as
 *  an ISO 8859 or other EUC encoding, or UTF-8, in which all
 *  bytes in the range 0x00 through 0x7F are ASCII characters and
 *  non-ASCII characters are constructed from one or more bytes in
 *  the range 0x80 through 0xFF.
 *
 * @param str The string to be upper-cased.
 * @return    ptr to the string
 */
WS_DLL_PUBLIC
gchar *ascii_strup_inplace(gchar *str);

/** Check if an entire string consists of printable characters
 *
 * @param str    The string to be checked
 * @return       TRUE if the entire string is printable, otherwise FALSE
 */
WS_DLL_PUBLIC
gboolean isprint_string(const gchar *str);

/** Check if an entire UTF-8 string consists of printable characters
 *
 * @param str    The string to be checked
 * @param length The number of bytes to validate
 * @return       TRUE if the entire string is printable, otherwise FALSE
 */
WS_DLL_PUBLIC
gboolean isprint_utf8_string(const gchar *str, guint length);

/** Check if an entire string consists of digits
 *
 * @param str    The string to be checked
 * @return       TRUE if the entire string is digits, otherwise FALSE
 */
WS_DLL_PUBLIC
gboolean isdigit_string(const guchar *str);

WS_DLL_PUBLIC
int ws_xton(char ch);

typedef enum {
    format_size_unit_none      = 0,     /**< No unit will be appended. You must supply your own. */
    format_size_unit_bytes     = 1,     /**< "bytes" for un-prefixed sizes, "B" otherwise. */
    format_size_unit_bits      = 2,     /**< "bits" for un-prefixed sizes, "b" otherwise. */
    format_size_unit_bits_s    = 3,     /**< "bits/s" for un-prefixed sizes, "bps" otherwise. */
    format_size_unit_bytes_s   = 4,     /**< "bytes/s" for un-prefixed sizes, "Bps" otherwise. */
    format_size_unit_packets   = 5,     /**< "packets" */
    format_size_unit_packets_s = 6,     /**< "packets/s" */
    format_size_prefix_si    = 0 << 8,  /**< SI (power of 1000) prefixes will be used. */
    format_size_prefix_iec   = 1 << 8   /**< IEC (power of 1024) prefixes will be used. */
    /* XXX format_size_prefix_default_for_this_particular_os ? */
} format_size_flags_e;

/** Given a size, return its value in a human-readable format
 *
 * Prefixes up to "T/Ti" (tera, tebi) are currently supported.
 *
 * @param size The size value
 * @param flags Flags to control the output (unit of measurement,
 * SI vs IEC, etc). Unit and prefix flags may be ORed together.
 * @return A newly-allocated string representing the value.
 */
WS_DLL_PUBLIC
gchar *format_size_wmem(wmem_allocator_t *allocator, gint64 size, format_size_flags_e flags);

#define format_size(size, flags)    format_size_wmem(NULL, size, flags)

WS_DLL_PUBLIC
gchar printable_char_or_period(gchar c);

/* To pass one of two strings, singular or plural */
#define plurality(d,s,p) ((d) == 1 ? (s) : (p))

#define true_or_false(val) ((val) ? "TRUE" : "FALSE")

#ifdef __cplusplus
}

/* Should we just have separate unit and prefix enums instead? */
extern format_size_flags_e operator|(format_size_flags_e lhs, format_size_flags_e rhs);
#endif /* __cplusplus */

#endif /* __STR_UTIL_H__ */
