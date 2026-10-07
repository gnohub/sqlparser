#ifndef SQLPARSER_ASCII_STRING_INTERNAL_H
#define SQLPARSER_ASCII_STRING_INTERNAL_H

#include <stdint.h>
#include <string.h>

/* Exactly one ordinary printable-ASCII string token, with no quote or
 * backslash in its contents. Length bounds every word load; memcpy avoids
 * alignment and aliasing requirements. Repeated-byte masks are endian-neutral.
 * Anything outside this deliberately small language still uses the parser. */
static int sqlparser_patch_plain_ascii_string_sql(const char *sql)
{
    const unsigned char *cursor;
    size_t length, remaining;

    if (sql == NULL || sql[0] != '\'') return 0;
    /* Retain the scalar recognizer's immediate decisions before asking for
     * the length. An early invalid byte must not require scanning a long tail. */
    cursor = (const unsigned char *)sql + 1U;
    if (*cursor == '\'') return cursor[1] == 0U;
    if (*cursor < 0x20U || *cursor > 0x7eU || *cursor == '\\') return 0;
    length = strlen(sql);
    if (length < 2U || sql[length - 1U] != '\'') return 0;
    cursor = (const unsigned char *)sql + 1U;
    remaining = length - 2U;
    while (remaining >= sizeof(uint64_t)) {
        const uint64_t ones = UINT64_C(0x0101010101010101);
        const uint64_t highs = UINT64_C(0x8080808080808080);
        uint64_t word, quote, slash, invalid;

        memcpy(&word, cursor, sizeof(word));
        quote = word ^ UINT64_C(0x2727272727272727);
        slash = word ^ UINT64_C(0x5c5c5c5c5c5c5c5c);
        /* A high bit rejects non-ASCII; subtraction rejects bytes below
         * space, and addition rejects DEL. Cross-byte borrows can only add
         * rejection bits after an already invalid byte. The final two terms
         * are the standard any-zero-byte test for either forbidden delimiter. */
        invalid = word | (word - UINT64_C(0x2020202020202020)) |
            (word + ones) | ((quote - ones) & ~quote) |
            ((slash - ones) & ~slash);
        if ((invalid & highs) != 0U) return 0;
        cursor += sizeof(word);
        remaining -= sizeof(word);
    }
    while (remaining-- > 0U) {
        unsigned char c = *cursor++;
        if (c < 0x20U || c > 0x7eU || c == '\'' || c == '\\') return 0;
    }
    return 1;
}

#endif
