/*
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 2001 Gerald Combs
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <stdlib.h>
#include <errno.h>
#include "ftypes-int.h"
#include <epan/addr_resolv.h>
#include <epan/strutil.h>
#include <epan/to_str.h>

#include <wsutil/pint.h>

static void
int_fvalue_new(fvalue_t *fv)
{
	fv->value.uinteger = 0;
}

static void
set_uinteger(fvalue_t *fv, guint32 value)
{
	fv->value.uinteger = value;
}

static void
set_sinteger(fvalue_t *fv, gint32 value)
{
	fv->value.sinteger = value;
}


static guint32
get_uinteger(fvalue_t *fv)
{
	return fv->value.uinteger;
}

static gint32
get_sinteger(fvalue_t *fv)
{
	return fv->value.sinteger;
}

gboolean
parse_charconst(const char *s, unsigned long *valuep, gchar **err_msg)
{
	const char *cp;
	unsigned long value;

	cp = s + 1;	/* skip the leading ' */
	if (*cp == '\\') {
		/*
		 * C escape sequence.
		 * An escape sequence is an octal number \NNN,
		 * an hex number \xNN, or one of \' \" \? \\ \a \b \f \n \r
		 * \t \v that stands for the byte value of the equivalent
		 * C-escape in ASCII encoding.
		 */
		cp++;
		switch (*cp) {

		case '\0':
			if (err_msg != NULL)
				*err_msg = g_strdup_printf("\"%s\" isn't a valid character constant.", s);
			return FALSE;

		case 'a':
			value = '\a';
			break;

		case 'b':
			value = '\b';
			break;

		case 'f':
			value = '\f';
			break;

		case 'n':
			value = '\n';
			break;

		case 'r':
			value = '\r';
			break;

		case 't':
			value = '\t';
			break;

		case 'v':
			value = '\v';
			break;

		case '\'':
			value = '\'';
			break;

		case '\\':
			value = '\\';
			break;

		case '"':
			value = '"';
			break;

		case 'x':
			cp++;
			if (*cp >= '0' && *cp <= '9')
				value = *cp - '0';
			else if (*cp >= 'A' && *cp <= 'F')
				value = 10 + (*cp - 'A');
			else if (*cp >= 'a' && *cp <= 'f')
				value = 10 + (*cp - 'a');
			else {
				if (err_msg != NULL)
					*err_msg = g_strdup_printf("\"%s\" isn't a valid character constant.", s);
				return FALSE;
			}
			cp++;
			if (*cp != '\'') {
				value <<= 4;
				if (*cp >= '0' && *cp <= '9')
					value |= *cp - '0';
				else if (*cp >= 'A' && *cp <= 'F')
					value |= 10 + (*cp - 'A');
				else if (*cp >= 'a' && *cp <= 'f')
					value |= 10 + (*cp - 'a');
				else {
					if (err_msg != NULL)
						*err_msg = g_strdup_printf("\"%s\" isn't a valid character constant.", s);
					return FALSE;
				}
			}
			break;

		default:
			/* Octal */
			if (*cp >= '0' && *cp <= '7')
				value = *cp - '0';
			else {
				if (err_msg != NULL)
					*err_msg = g_strdup_printf("\"%s\" isn't a valid character constant.", s);
				return FALSE;
			}
			if (*(cp + 1) != '\'') {
				cp++;
				value <<= 3;
				if (*cp >= '0' && *cp <= '7')
					value |= *cp - '0';
				else {
					if (err_msg != NULL)
						*err_msg = g_strdup_printf("\"%s\" isn't a valid character constant.", s);
					return FALSE;
				}
				if (*(cp + 1) != '\'') {
					cp++;
					value <<= 3;
					if (*cp >= '0' && *cp <= '7')
						value |= *cp - '0';
					else {
						if (err_msg != NULL)
							*err_msg = g_strdup_printf("\"%s\" isn't a valid character constant.", s);
						return FALSE;
					}
				}
			}
			if (value > 0xFF) {
				if (err_msg != NULL)
					*err_msg = g_strdup_printf("\"%s\" is too large to be a valid character constant.", s);
				return FALSE;
			}
		}
	} else {
		value = *cp;
		if (!g_ascii_isprint(value)) {
			if (err_msg != NULL)
				*err_msg = g_strdup_printf("Non-printable character '\\x%02lx' in character constant.", value);
			return FALSE;
		}
	}
	cp++;
	if ((*cp != '\'') || (*(cp + 1) != '\0')){
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" is too long to be a valid character constant.", s);
		return FALSE;
	}

	*valuep = value;
	return TRUE;
}

static gboolean
uint_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value _U_, gchar **err_msg,
		   guint32 max)
{
	unsigned long value;
	char	*endptr;

	if (s[0] == '\'') {
		/*
		 * Represented as a C-style character constant.
		 */
		if (!parse_charconst(s, &value, err_msg))
			return FALSE;
	} else {
		/*
		 * Try to parse it as a number.
		 */
		if (strchr (s, '-') && strtol(s, NULL, 0) < 0) {
			/*
			 * Probably a negative integer, but will be
			 * "converted in the obvious manner" by strtoul().
			 */
			if (err_msg != NULL)
				*err_msg = g_strdup_printf("\"%s\" too small for this field, minimum 0.", s);
			return FALSE;
		}

		errno = 0;
		value = strtoul(s, &endptr, 0);

		if (errno == EINVAL || endptr == s || *endptr != '\0') {
			/* This isn't a valid number. */
			if (err_msg != NULL)
				*err_msg = g_strdup_printf("\"%s\" is not a valid number.", s);
			return FALSE;
		}
		if (errno == ERANGE) {
			if (err_msg != NULL) {
				if (value == ULONG_MAX) {
					*err_msg = g_strdup_printf("\"%s\" causes an integer overflow.",
					    s);
				}
				else {
					/*
					 * XXX - can "strtoul()" set errno to
					 * ERANGE without returning ULONG_MAX?
					 */
					*err_msg = g_strdup_printf("\"%s\" is not an integer.", s);
				}
			}
			return FALSE;
		}
	}

	if (value > max) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" too big for this field, maximum %u.", s, max);
		return FALSE;
	}

	fv->value.uinteger = (guint32)value;
	return TRUE;
}

static gboolean
uint32_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return uint_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXUINT32);
}

static gboolean
uint24_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return uint_from_unparsed (fv, s, allow_partial_value, err_msg, 0xFFFFFF);
}

static gboolean
uint16_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return uint_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXUINT16);
}

static gboolean
uint8_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return uint_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXUINT8);
}

static gboolean
sint_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value _U_, gchar **err_msg,
		   gint32 max, gint32 min)
{
	long value;
	unsigned long charvalue;
	char *endptr;

	if (s[0] == '\'') {
		/*
		 * Represented as a C-style character constant.
		 */
		if (!parse_charconst(s, &charvalue, err_msg))
			return FALSE;

		/*
		 * The FT_CHAR type is defined to be signed, regardless
		 * of whether char is signed or unsigned, so cast the value
		 * to "signed char".
		 */
		value = (signed char)charvalue;
	} else {
		/*
		 * Try to parse it as a number.
		 */
		if (!strchr (s, '-') && strtoul(s, NULL, 0) > G_MAXINT32) {
			/*
			 * Probably a positive integer > G_MAXINT32, but
			 * will be "converted in the obvious manner" by
			 * strtol().
			 */
			if (err_msg != NULL)
				*err_msg = g_strdup_printf("\"%s\" causes an integer overflow.", s);
			return FALSE;
		}

		errno = 0;
		value = strtol(s, &endptr, 0);

		if (errno == EINVAL || endptr == s || *endptr != '\0') {
			/* This isn't a valid number. */
			if (err_msg != NULL)
				*err_msg = g_strdup_printf("\"%s\" is not a valid number.", s);
			return FALSE;
		}
		if (errno == ERANGE) {
			if (err_msg != NULL) {
				if (value == LONG_MAX) {
					*err_msg = g_strdup_printf("\"%s\" causes an integer overflow.", s);
				}
				else if (value == LONG_MIN) {
					*err_msg = g_strdup_printf("\"%s\" causes an integer underflow.", s);
				}
				else {
					/*
					 * XXX - can "strtol()" set errno to
					 * ERANGE without returning ULONG_MAX?
					 */
					*err_msg = g_strdup_printf("\"%s\" is not an integer.", s);
				}
			}
			return FALSE;
		}
	}

	if (value > max) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" too big for this field, maximum %d.",
				s, max);
		return FALSE;
	} else if (value < min) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" too small for this field, minimum %d.",
				s, min);
		return FALSE;
	}

	fv->value.sinteger = (gint32)value;
	return TRUE;
}

static gboolean
sint32_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return sint_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXINT32, G_MININT32);
}

static gboolean
sint24_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return sint_from_unparsed (fv, s, allow_partial_value, err_msg, 0x7FFFFF, -0x800000);
}

static gboolean
sint16_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return sint_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXINT16, G_MININT16);
}

static gboolean
sint8_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return sint_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXINT8, G_MININT8);
}

static int
integer_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 11;	/* enough for 2^31-1, in decimal */
}

static void
integer_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display _U_, char *buf, unsigned int size)
{
	guint32 val;

	if (fv->value.sinteger < 0) {
		*buf++ = '-';
		val = -fv->value.sinteger;
	} else
		val = fv->value.sinteger;

	guint32_to_str_buf(val, buf, size);
}

static int
uinteger_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 10;	/* enough for 2^32-1, in decimal or 0xXXXXXXXX */
}

static int
char_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 7;	/* enough for '\OOO' or '\xXX' */
}

static void
uinteger_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display, char *buf, unsigned int size)
{
	if (((field_display & 0xff) == BASE_HEX) || ((field_display & 0xff) == BASE_HEX_DEC))
	{
		/* This format perfectly fits into 11 bytes. */
		*buf++ = '0';
		*buf++ = 'x';

		switch (fv->ftype->ftype) {

		case FT_UINT8:
			buf = guint8_to_hex(buf, fv->value.uinteger);
			break;

		case FT_UINT16:
			buf = word_to_hex(buf, fv->value.uinteger);
			break;

		case FT_UINT24:
			buf = guint8_to_hex(buf, (fv->value.uinteger & 0x00ff0000) >> 16);
			buf = word_to_hex(buf, (fv->value.uinteger & 0x0000ffff));
			break;

		default:
			buf = dword_to_hex(buf, fv->value.uinteger);
			break;
		}

		*buf++ = '\0';
	}
	else
	{
		guint32_to_str_buf(fv->value.uinteger, buf, size);
	}
}

static void
char_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display, char *buf, unsigned int size _U_)
{
	/*
	 * The longest possible strings are "'\OOO'" and "'\xXX'", which
	 * take 7 bytes, including the terminating '\0'.
	 */
	*buf++ = '\'';
	if (g_ascii_isprint(fv->value.uinteger)) {
		/* This perfectly fits into 4 or 5 bytes. */
		if (fv->value.uinteger == '\\' || fv->value.uinteger == '\'')
			*buf++ = '\\';
		*buf++ = (char)fv->value.uinteger;
	} else {
		*buf++ = '\\';
		switch (fv->value.uinteger) {

		case '\0':
			*buf++ = '0';
			break;

		case '\a':
			*buf++ = 'a';
			break;

		case '\b':
			*buf++ = 'b';
			break;

		case '\f':
			*buf++ = 'f';
			break;

		case '\n':
			*buf++ = 'n';
			break;

		case '\r':
			*buf++ = 'r';
			break;

		case '\t':
			*buf++ = 't';
			break;

		case '\v':
			*buf++ = 'v';
			break;

		default:
			if (field_display == BASE_HEX) {
				*buf++ = 'x';
				buf = guint8_to_hex(buf, fv->value.uinteger);
			}
			else
			{
				*buf++ = ((fv->value.uinteger >> 6) & 0x7) + '0';
				*buf++ = ((fv->value.uinteger >> 3) & 0x7) + '0';
				*buf++ = ((fv->value.uinteger >> 0) & 0x7) + '0';
			}
			break;
		}
	}
	*buf++ = '\'';
	*buf++ = '\0';
}

static gboolean
ipxnet_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value _U_, gchar **err_msg)
{
	/*
	 * Don't request an error message if bytes_from_unparsed fails;
	 * if it does, we'll report an error specific to this address
	 * type.
	 */
	if (uint32_from_unparsed(fv, s, TRUE, NULL)) {
		return TRUE;
	}

	/* XXX - Try resolving as an IPX host name and parse that? */

	if (err_msg != NULL)
		*err_msg = g_strdup_printf("\"%s\" is not a valid IPX network address.", s);
	return FALSE;
}

static int
ipxnet_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 2+8;	/* 0xXXXXXXXX */
}

static void
ipxnet_to_repr(const fvalue_t *fv, ftrepr_t rtype, int field_display _U_, char *buf, unsigned int size)
{
	uinteger_to_repr(fv, rtype, BASE_HEX, buf, size);
}

static int
uinteger_cmp_order(const fvalue_t *a, const fvalue_t *b)
{
	if (a->value.uinteger == b->value.uinteger)
		return 0;
	return a->value.uinteger < b->value.uinteger ? -1 : 1;
}

static int
sinteger_cmp_order(const fvalue_t *a, const fvalue_t *b)
{
	if (a->value.sinteger == b->value.sinteger)
		return 0;
	return a->value.sinteger < b->value.sinteger ? -1 : 1;
}

static int
uinteger64_cmp_order(const fvalue_t *a, const fvalue_t *b)
{
	if (a->value.uinteger64 == b->value.uinteger64)
		return 0;
	return a->value.uinteger64 < b->value.uinteger64 ? -1 : 1;
}

static int
sinteger64_cmp_order(const fvalue_t *a, const fvalue_t *b)
{
	if (a->value.sinteger64 == b->value.sinteger64)
		return 0;
	return a->value.sinteger64 < b->value.sinteger64 ? -1 : 1;
}

static gboolean
cmp_bitwise_and(const fvalue_t *a, const fvalue_t *b)
{
	return ((a->value.uinteger & b->value.uinteger) != 0);
}

static void
int64_fvalue_new(fvalue_t *fv)
{
	fv->value.sinteger64 = 0;
}

static void
set_uinteger64(fvalue_t *fv, guint64 value)
{
	fv->value.uinteger64 = value;
}

static void
set_sinteger64(fvalue_t *fv, gint64 value)
{
	fv->value.sinteger64 = value;
}

static guint64
get_uinteger64(fvalue_t *fv)
{
	return fv->value.uinteger64;
}

static gint64
get_sinteger64(fvalue_t *fv)
{
	return fv->value.sinteger64;
}

static gboolean
_uint64_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value _U_, gchar **err_msg,
		   guint64 max)
{
	guint64 value;
	char	*endptr;

	if (strchr (s, '-') && g_ascii_strtoll(s, NULL, 0) < 0) {
		/*
		 * Probably a negative integer, but will be
		 * "converted in the obvious manner" by g_ascii_strtoull().
		 */
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" causes an integer underflow.", s);
		return FALSE;
	}

	errno = 0;
	value = g_ascii_strtoull(s, &endptr, 0);

	if (errno == EINVAL || endptr == s || *endptr != '\0') {
		/* This isn't a valid number. */
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" is not a valid number.", s);
		return FALSE;
	}
	if (errno == ERANGE) {
		if (err_msg != NULL) {
			if (value == G_MAXUINT64) {
				*err_msg = g_strdup_printf("\"%s\" causes an integer overflow.", s);
			}
			else {
				/*
				 * XXX - can "strtoul()" set errno to
				 * ERANGE without returning ULONG_MAX?
				 */
				*err_msg = g_strdup_printf("\"%s\" is not an integer.", s);
			}
		}
		return FALSE;
	}

	if (value > max) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" too big for this field, maximum %" G_GINT64_MODIFIER "u.", s, max);
		return FALSE;
	}

	fv->value.uinteger64 = value;
	return TRUE;
}

static gboolean
uint64_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _uint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXUINT64);
}

static gboolean
uint56_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _uint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_GUINT64_CONSTANT(0xFFFFFFFFFFFFFF));
}

static gboolean
uint48_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _uint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_GUINT64_CONSTANT(0xFFFFFFFFFFFF));
}

static gboolean
uint40_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _uint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_GUINT64_CONSTANT(0xFFFFFFFFFF));
}

static gboolean
_sint64_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value _U_, gchar **err_msg,
		   gint64 max, gint64 min)
{
	gint64 value;
	char   *endptr;

	if (!strchr (s, '-') && g_ascii_strtoull(s, NULL, 0) > G_MAXINT64) {
		/*
		 * Probably a positive integer > G_MAXINT64, but will be
		 * "converted in the obvious manner" by g_ascii_strtoll().
		 */
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" causes an integer overflow.", s);
		return FALSE;
	}

	errno = 0;
	value = g_ascii_strtoll(s, &endptr, 0);

	if (errno == EINVAL || endptr == s || *endptr != '\0') {
		/* This isn't a valid number. */
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" is not a valid number.", s);
		return FALSE;
	}
	if (errno == ERANGE) {
		if (err_msg != NULL) {
			if (value == G_MAXINT64) {
				*err_msg = g_strdup_printf("\"%s\" causes an integer overflow.", s);
			}
			else if (value == G_MININT64) {
				*err_msg = g_strdup_printf("\"%s\" causes an integer underflow.", s);
			}
			else {
				/*
				 * XXX - can "strtol()" set errno to
				 * ERANGE without returning LONG_MAX?
				 */
				*err_msg = g_strdup_printf("\"%s\" is not an integer.", s);
			}
		}
		return FALSE;
	}

	if (value > max) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" too big for this field, maximum %" G_GINT64_MODIFIER "u.", s, max);
		return FALSE;
	} else if (value < min) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" too small for this field, maximum %" G_GINT64_MODIFIER "u.", s, max);
		return FALSE;
	}

	fv->value.sinteger64 = (guint64)value;
	return TRUE;
}

static gboolean
sint64_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _sint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_MAXINT64, G_MININT64);
}

static gboolean
sint56_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _sint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_GINT64_CONSTANT(0x7FFFFFFFFFFFFF), G_GINT64_CONSTANT(-0x80000000000000));
}

static gboolean
sint48_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _sint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_GINT64_CONSTANT(0x7FFFFFFFFFFF), G_GINT64_CONSTANT(-0x800000000000));
}

static gboolean
sint40_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value, gchar **err_msg)
{
	return _sint64_from_unparsed (fv, s, allow_partial_value, err_msg, G_GINT64_CONSTANT(0x7FFFFFFFFF), G_GINT64_CONSTANT(-0x8000000000));
}

static int
integer64_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 20;	/* enough for -2^63-1, in decimal */
}

static void
integer64_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display _U_, char *buf, unsigned int size)
{
	guint64 val;

	if (fv->value.sinteger64 < 0) {
		*buf++ = '-';
		val = -fv->value.sinteger64;
	} else
		val = fv->value.sinteger64;

	guint64_to_str_buf(val, buf, size);
}

static int
uinteger64_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 20;	/* enough for 2^64-1, in decimal or 0xXXXXXXXXXXXXXXXX */
}

static void
uinteger64_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display, char *buf, unsigned int size)
{
	if ((field_display == BASE_HEX) || (field_display == BASE_HEX_DEC))
	{
		/* This format perfectly fits into 19 bytes. */
		*buf++ = '0';
		*buf++ = 'x';

		buf = qword_to_hex(buf, fv->value.uinteger64);
		*buf++ = '\0';
	}
	else
	{
		guint64_to_str_buf(fv->value.uinteger64, buf, size);
	}
}

static gboolean
cmp_bitwise_and64(const fvalue_t *a, const fvalue_t *b)
{
	return ((a->value.uinteger64 & b->value.uinteger64) != 0);
}

/* BOOLEAN-specific */

static void
boolean_fvalue_new(fvalue_t *fv)
{
	fv->value.uinteger64 = TRUE;
}

static int
boolean_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return 1;
}

static void
boolean_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display _U_, char *buf, unsigned int size _U_)
{
	*buf++ = (fv->value.uinteger64) ? '1' : '0';
	*buf   = '\0';
}

/* False is less than True (arbitrary):
 * A  B   cmp(A, B)
 * T  T   0
 * F  F   0
 * F  T  -1
 * T  F   1
 */
static int
bool_cmp_order(const fvalue_t *a, const fvalue_t *b)
{
	if (a->value.uinteger64) {
		if (b->value.uinteger64) {
			return 0;
		}
		return 1;
	}
	if (b->value.uinteger64) {
		return -1;
	}
	return 0;
}

/* EUI64-specific */
static gboolean
eui64_from_unparsed(fvalue_t *fv, const char *s, gboolean allow_partial_value _U_, gchar **err_msg)
{
	GByteArray	*bytes;
	gboolean	res;
	union {
		guint64 value;
		guint8  bytes[8];
	} eui64;

	/*
	 * Don't request an error message if uint64_from_unparsed fails;
	 * if it does, we'll try parsing it as a sequence of bytes, and
	 * report an error if *that* fails.
	 */
	if (uint64_from_unparsed(fv, s, TRUE, NULL)) {
		return TRUE;
	}

	bytes = g_byte_array_new();
	res = hex_str_to_bytes(s, bytes, TRUE);
	if (!res || bytes->len != 8) {
		if (err_msg != NULL)
			*err_msg = g_strdup_printf("\"%s\" is not a valid EUI-64 address.", s);
		g_byte_array_free(bytes, TRUE);
		return FALSE;
	}

	memcpy(eui64.bytes, bytes->data, 8);
	g_byte_array_free(bytes, TRUE);
	fv->value.integer64 = GUINT64_FROM_BE(eui64.value);
	return TRUE;
}

static int
eui64_repr_len(const fvalue_t *fv _U_, ftrepr_t rtype _U_, int field_display _U_)
{
	return EUI64_STR_LEN;	/* XX:XX:XX:XX:XX:XX:XX:XX */
}

static void
eui64_to_repr(const fvalue_t *fv, ftrepr_t rtype _U_, int field_display _U_, char *buf, unsigned int size)
{
	union {
		guint64 value;
		guint8  bytes[8];
	} eui64;

	/* Copy and convert the address from host to network byte order. */
	eui64.value = GUINT64_TO_BE(fv->value.integer64);

	g_snprintf(buf, size, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
	    eui64.bytes[0], eui64.bytes[1], eui64.bytes[2], eui64.bytes[3],
	    eui64.bytes[4], eui64.bytes[5], eui64.bytes[6], eui64.bytes[7]);
}

void
ftype_register_integers(void)
{
	static ftype_t char_type = {
		FT_CHAR,			/* ftype */
		"FT_CHAR",			/* name */
		"Character, 1 byte",		/* pretty name */
		1,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		uint8_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		char_to_repr,			/* val_to_string_repr */
		char_repr_len,			/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t uint8_type = {
		FT_UINT8,			/* ftype */
		"FT_UINT8",			/* name */
		"Unsigned integer, 1 byte",	/* pretty name */
		1,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		uint8_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger_to_repr,		/* val_to_string_repr */
		uinteger_repr_len,		/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t uint16_type = {
		FT_UINT16,			/* ftype */
		"FT_UINT16",			/* name */
		"Unsigned integer, 2 bytes",	/* pretty_name */
		2,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		uint16_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger_to_repr,		/* val_to_string_repr */
		uinteger_repr_len,		/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t uint24_type = {
		FT_UINT24,			/* ftype */
		"FT_UINT24",			/* name */
		"Unsigned integer, 3 bytes",	/* pretty_name */
		3,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		uint24_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger_to_repr,		/* val_to_string_repr */
		uinteger_repr_len,		/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t uint32_type = {
		FT_UINT32,			/* ftype */
		"FT_UINT32",			/* name */
		"Unsigned integer, 4 bytes",	/* pretty_name */
		4,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		uint32_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger_to_repr,		/* val_to_string_repr */
		uinteger_repr_len,		/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t uint40_type = {
		FT_UINT40,			/* ftype */
		"FT_UINT40",			/* name */
		"Unsigned integer, 5 bytes",	/* pretty_name */
		5,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		uint40_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger64_to_repr,		/* val_to_string_repr */
		uinteger64_repr_len,		/* len_string_repr */

		{ .set_value_uinteger64 = set_uinteger64 },	/* union set_value */
		{ .get_value_uinteger64 = get_uinteger64 },	/* union get_value */

		uinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t uint48_type = {
		FT_UINT48,			/* ftype */
		"FT_UINT48",			/* name */
		"Unsigned integer, 6 bytes",	/* pretty_name */
		6,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		uint48_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger64_to_repr,		/* val_to_string_repr */
		uinteger64_repr_len,		/* len_string_repr */

		{ .set_value_uinteger64 = set_uinteger64 },	/* union set_value */
		{ .get_value_uinteger64 = get_uinteger64 },	/* union get_value */

		uinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t uint56_type = {
		FT_UINT56,			/* ftype */
		"FT_UINT56",			/* name */
		"Unsigned integer, 7 bytes",	/* pretty_name */
		7,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		uint56_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger64_to_repr,		/* val_to_string_repr */
		uinteger64_repr_len,		/* len_string_repr */

		{ .set_value_uinteger64 = set_uinteger64 },	/* union set_value */
		{ .get_value_uinteger64 = get_uinteger64 },	/* union get_value */

		uinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t uint64_type = {
		FT_UINT64,			/* ftype */
		"FT_UINT64",			/* name */
		"Unsigned integer, 8 bytes",	/* pretty_name */
		8,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		uint64_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger64_to_repr,		/* val_to_string_repr */
		uinteger64_repr_len,		/* len_string_repr */

		{ .set_value_uinteger64 = set_uinteger64 },	/* union set_value */
		{ .get_value_uinteger64 = get_uinteger64 },	/* union get_value */

		uinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t int8_type = {
		FT_INT8,			/* ftype */
		"FT_INT8",			/* name */
		"Signed integer, 1 byte",	/* pretty_name */
		1,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		sint8_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer_to_repr,		/* val_to_string_repr */
		integer_repr_len,		/* len_string_repr */

		{ .set_value_sinteger = set_sinteger },	/* union set_value */
		{ .get_value_sinteger = get_sinteger },	/* union get_value */

		sinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t int16_type = {
		FT_INT16,			/* ftype */
		"FT_INT16",			/* name */
		"Signed integer, 2 bytes",	/* pretty_name */
		2,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		sint16_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer_to_repr,		/* val_to_string_repr */
		integer_repr_len,		/* len_string_repr */

		{ .set_value_sinteger = set_sinteger },	/* union set_value */
		{ .get_value_sinteger = get_sinteger },	/* union get_value */

		sinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t int24_type = {
		FT_INT24,			/* ftype */
		"FT_INT24",			/* name */
		"Signed integer, 3 bytes",	/* pretty_name */
		3,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		sint24_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer_to_repr,		/* val_to_string_repr */
		integer_repr_len,		/* len_string_repr */

		{ .set_value_sinteger = set_sinteger },	/* union set_value */
		{ .get_value_sinteger = get_sinteger },	/* union get_value */

		sinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t int32_type = {
		FT_INT32,			/* ftype */
		"FT_INT32",			/* name */
		"Signed integer, 4 bytes",	/* pretty_name */
		4,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		sint32_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer_to_repr,		/* val_to_string_repr */
		integer_repr_len,		/* len_string_repr */

		{ .set_value_sinteger = set_sinteger },	/* union set_value */
		{ .get_value_sinteger = get_sinteger },	/* union get_value */

		sinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};
	static ftype_t int40_type = {
		FT_INT40,			/* ftype */
		"FT_INT40",			/* name */
		"Signed integer, 5 bytes",	/* pretty_name */
		5,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		sint40_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer64_to_repr,		/* val_to_string_repr */
		integer64_repr_len,		/* len_string_repr */

		{ .set_value_sinteger64 = set_sinteger64 },	/* union set_value */
		{ .get_value_sinteger64 = get_sinteger64 },	/* union get_value */

		sinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t int48_type = {
		FT_INT48,			/* ftype */
		"FT_INT48",			/* name */
		"Signed integer, 6 bytes",	/* pretty_name */
		6,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		sint48_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer64_to_repr,		/* val_to_string_repr */
		integer64_repr_len,		/* len_string_repr */

		{ .set_value_sinteger64 = set_sinteger64 },	/* union set_value */
		{ .get_value_sinteger64 = get_sinteger64 },	/* union get_value */

		sinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t int56_type = {
		FT_INT56,			/* ftype */
		"FT_INT56",			/* name */
		"Signed integer, 7 bytes",	/* pretty_name */
		7,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		sint56_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer64_to_repr,		/* val_to_string_repr */
		integer64_repr_len,		/* len_string_repr */

		{ .set_value_sinteger64 = set_sinteger64 },	/* union set_value */
		{ .get_value_sinteger64 = get_sinteger64 },	/* union get_value */

		sinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t int64_type = {
		FT_INT64,			/* ftype */
		"FT_INT64",			/* name */
		"Signed integer, 8 bytes",	/* pretty_name */
		8,				/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		sint64_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		integer64_to_repr,		/* val_to_string_repr */
		integer64_repr_len,		/* len_string_repr */

		{ .set_value_sinteger64 = set_sinteger64 },	/* union set_value */
		{ .get_value_sinteger64 = get_sinteger64 },	/* union get_value */

		sinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};
	static ftype_t boolean_type = {
		FT_BOOLEAN,			/* ftype */
		"FT_BOOLEAN",			/* name */
		"Boolean",			/* pretty_name */
		0,				/* wire_size */
		boolean_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		uint64_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		boolean_to_repr,		/* val_to_string_repr */
		boolean_repr_len,		/* len_string_repr */

		{ .set_value_uinteger64 = set_uinteger64 },	/* union set_value */
		{ .get_value_uinteger64 = get_uinteger64 },	/* union get_value */

		bool_cmp_order,			/* cmp_eq */
		NULL,				/* cmp_bitwise_and */
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};

	static ftype_t ipxnet_type = {
		FT_IPXNET,			/* ftype */
		"FT_IPXNET",			/* name */
		"IPX network number",		/* pretty_name */
		4,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		ipxnet_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		ipxnet_to_repr,			/* val_to_string_repr */
		ipxnet_repr_len,		/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		cmp_bitwise_and,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};

	static ftype_t framenum_type = {
		FT_FRAMENUM,			/* ftype */
		"FT_FRAMENUM",			/* name */
		"Frame number",			/* pretty_name */
		4,				/* wire_size */
		int_fvalue_new,			/* new_value */
		NULL,				/* free_value */
		uint32_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		uinteger_to_repr,		/* val_to_string_repr */
		uinteger_repr_len,		/* len_string_repr */

		{ .set_value_uinteger = set_uinteger },	/* union set_value */
		{ .get_value_uinteger = get_uinteger },	/* union get_value */

		uinteger_cmp_order,
		NULL,				/* cmp_bitwise_and */
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,				/* len */
		NULL,				/* slice */
	};

	static ftype_t eui64_type = {
		FT_EUI64,			/* ftype */
		"FT_EUI64",			/* name */
		"EUI64 address",		/* pretty_name */
		FT_EUI64_LEN,			/* wire_size */
		int64_fvalue_new,		/* new_value */
		NULL,				/* free_value */
		eui64_from_unparsed,		/* val_from_unparsed */
		NULL,				/* val_from_string */
		eui64_to_repr,			/* val_to_string_repr */
		eui64_repr_len,			/* len_string_repr */

		{ .set_value_uinteger64 = set_uinteger64 },	/* union set_value */
		{ .get_value_uinteger64 = get_uinteger64 },	/* union get_value */

		uinteger64_cmp_order,
		cmp_bitwise_and64,
		NULL,				/* cmp_contains */
		NULL,				/* cmp_matches */

		NULL,
		NULL,
	};

	ftype_register(FT_CHAR, &char_type);
	ftype_register(FT_UINT8, &uint8_type);
	ftype_register(FT_UINT16, &uint16_type);
	ftype_register(FT_UINT24, &uint24_type);
	ftype_register(FT_UINT32, &uint32_type);
	ftype_register(FT_UINT40, &uint40_type);
	ftype_register(FT_UINT48, &uint48_type);
	ftype_register(FT_UINT56, &uint56_type);
	ftype_register(FT_UINT64, &uint64_type);
	ftype_register(FT_INT8, &int8_type);
	ftype_register(FT_INT16, &int16_type);
	ftype_register(FT_INT24, &int24_type);
	ftype_register(FT_INT32, &int32_type);
	ftype_register(FT_INT40, &int40_type);
	ftype_register(FT_INT48, &int48_type);
	ftype_register(FT_INT56, &int56_type);
	ftype_register(FT_INT64, &int64_type);
	ftype_register(FT_BOOLEAN, &boolean_type);
	ftype_register(FT_IPXNET, &ipxnet_type);
	ftype_register(FT_FRAMENUM, &framenum_type);
	ftype_register(FT_EUI64, &eui64_type);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
