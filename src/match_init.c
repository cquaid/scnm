#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "match.h"
#include "match_internal.h"

/**
 * @file match_init.c
 *
 * Initialization routines for match.h objects.
 */

static bool
match_flags_set_integer(const char *value, union match_flags *flags)
{
    int neg;
    int64_t sval;
    uint64_t val;
    char *endptr = NULL;

    errno = 0;
    val = (uint64_t)strtoull(value, &endptr, 0);

    if ((errno != 0) || (*endptr != '\0'))
        return false;

    sval = *(int8_t *)&val;

    /* Determine if we were given a negative value. */
    neg = (sval < 0LL);

    /* For negative values, we know the given number
     * is < 0, so only check if the parsed result is
     * smaller than the signed type minimum. An upper
     * bound check would be redundant.
     */

    if (val <= UINT8_MAX) {
        if (neg)
            flags->i8 = !(sval < INT8_MIN);
        else
            flags->i8 = 1;
    }

    if (val <= UINT16_MAX) {
        if (neg)
            flags->i16 = !(sval < INT16_MIN);
        else
            flags->i16 = 1;
    }

    if (val <= UINT32_MAX) {
        if (neg)
            flags->i32 = !(sval < INT32_MIN);
        else
            flags->i32 = 1;
    }

    /* A value can always be a 64 bit integer. */
    flags->i64 = 1;

    return true;
}

static bool
match_flags_set_floating(const char *value, union match_flags *flags)
{
    float f;
    double d;
    char *endptr;

    /* Only if the value can be parsed explicitly as a float,
     * is this value capable of being an f32. */

    errno = 0;
    endptr = NULL;
    f = strtof(value, &endptr);
    (void)f;

    /* This would result in a parse falure for double as well
     * so just fail out. */
    if (endptr != '\0')
        return false;

    if (errno == 0) {
        flags->f32 = 1;
        flags->f64 = 1;
        return true;
    }

    /* If float parsing failed, attempt double. */

    errno = 0;
    endptr = NULL;
    d = strtod(value, &endptr);
    (void)d;

    if ((errno != 0) && (*endptr != '\0'))
        return false;

    flags->f64 = 1;

    return true;
}


/**
 * Initialize a match_needle object given an integer or
 * floating point number as an ascii string.
 *
 * @param[out] needle - object to initialize
 * @param[in] value - supplied value to convert and check
 *
 * @return 0 on success
 * @return < 0 on failure with error in errno
 */
int
match_needle_init(struct match_needle *needle, const char *value)
{
    double fval;
    uint64_t ival;

    char *endptr;

    memset(needle, 0, sizeof(*needle));

    /* Try parsing as an integer. */

    errno = 0;
    endptr = NULL;

    ival = (uint64_t)strtoull(value, &endptr, 0);

    /* Unfortunately, EINVAL can be returned if it cannot
     * convert as well... so just check the other error cases. */
    if (errno == ERANGE)
        return -1;

    if (endptr == '\0') {
        /* Ignore regurn.  We already know it parses correctly. */
        (void)match_flags_set_integer(value, &(needle->obj.flags));
        needle->obj.v.u64 = ival;
        return 0;
    }

    /* Try parsing as a floating point. */

    fval = (uint64_t)strtod(value, &endptr);

    if (errno == ERANGE)
        return -1;

    if (endptr == '\0') {
        /* Ignore regurn.  We already know it parses correctly. */
        (void)match_flags_set_floating(value, &(needle->obj.flags));
        needle->obj.v.f64 = fval;
        return 0;
    }

    /* No clue what to do with this.
     * Eventually support AOB and strings, though
     * likely through a different interface. */
    errno = EINVAL;
    return -1;
}

/**
 * Clear a match list object.
 *
 * @param list - list to clear
 */
void
match_list_clear(struct match_list *list)
{
    struct list_head *next;
    struct list_head *entry;

    list_for_each_safe(entry, next, &(list->head)) {
        struct match_chunk_header *header;

        list_del(entry);
        header = match_chunk_entry(entry);
        free(header);
    }

    match_list_init(list);
}

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
