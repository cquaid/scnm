#include <sys/types.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "shared/list.h"
#include "shared/util.h"
#include "ptracer/ptracer.h"

#include "match.h"
#include "match_internal.h"
#include "ptrace.h"
#include "region.h"


/**
 * @file match_ptrace.c
 *
 * Memory searching callback routines using ptrace
 *
 * TODO: create and supply a wintermute context holding a ptracer context.
 */

#define WINDOW_SIZE    (WINDOW_ENTRIES * sizeof(unsigned long))
#define WINDOW_ENTRIES (NLONG_PER_U64 * 2)
#define NLONG_PER_U64  ((sizeof(uint64_t) / sizeof(unsigned long))

struct __process_ptrace_data {
    union {
        unsigned long l[WINDOW_ENTRIES];
        char *data[WINDOW_SIZE];
    } window;

    size_t window_pos;
    size_t window_size;

    unsigned long addr;
    size_t remaining;
};


static int
__process_ptrace_init(struct process_ctx *ctx, int fd,
    pid_t pid, int aligned)
{
    /* Don't memset. We don't want to overwrite the ops. */

    ctx->fd = fd;
    ctx->pid = pid;
    ctx->aligned = aligned;

    ctx->data = calloc(1, sizeof(*data));

    if (ctx->data == NULL)
        return -1;

    return 0;
}

static void
__process_ptrace_fini(struct process_ctx *ctx)
{
    if (ctx->data != NULL) {
        free(ctx->data);
        ctx->data = NULL;
    }
}

static inline int
get_next_segment(struct process_ctx *ctx)
{
    struct __process_ptrace_data *data;

    data = ctx->data;

    /* Verify there is another segment to pull. */
    if (data->remaining < sizeof(unsigned long))
        return 1;

    if (data->window_len >= ARRAY_SIZ(data->window.l)) {
        size_t i;
        /* window_len == array size ; slide the window */
        for (i = 1; i < data->window_len; ++i)
            data->window.l[i - 1] = data->window.l[i];

        data->window_len = ARRAY_SIZ(data->window.l) - 1;
    }

    err = ptrace_peektext(ctx->pid, ctx->addr,
                &(data->window.l[ data->window_len ]);

    if (err != 0)
        return -1;

    data->addr += sizeof(unsigned long);
    data->remaining -= sizeof(unsigned long);
    data->window_len++;

    return 0;
}

/**
 * Grab the next 64b chunk using ptrace.
 *
 * @param ctx - processing context
 *
 * @return < 0 on failure
 * @return number read on success
 */
static inline int
get_next_u64(struct process_ctx *ctx)
{
    int i;
    int err;

    for (i = 0; i < NLONG_PER_U64; ++i) {
        err = get_next_segment(ctx);

        if (err < 0)
            return err;

        /* In the event that we failed,
         * we want to return the number we read.
         * on the next call to this function,
         * 0 will be returned and processing will stop.
         */
        if (err > 0)
            return i;
    }

    return NLONG_PER_U64;
}


static inline int
__process_ptrace_next_aligned(struct process_ctx *ctx,
    struct __process_ptrace_data *data, struct match_object *obj)
{
    /* When aligned, window_pos is used as an index into window.l[]. */

    if (data->window_pos >= ARRAY_SIZ(data->window.l)) {
        int err;

        err = get_next_u64(ctx);

        if (err < 0)
            return err;

        if (err == 0)
            return 1;

        data->window_pos = ARRAY_SIZ(data->window.l) - err;

        /* Shift down so we slide on proper aligned boundaries.
         *
         * Example: (assume long == 32b)
         *
         * ^ -- window_pos
         *
         * | 32b | 32b | 32b | 32b | <end>
         *                             ^
         * Then slide 2 (NLONG_PER_U64)
         *
         * | 32b | 32b | new | new | <end>
         *                ^
         * Well, since the alignment is 32b not 64, we need to fixup
         * and slide 1 more (NLONG_PER_U64 - 1).
         *
         * | 32b | 32b | new | new | <end>
         *          ^
         */
        if (NLONG_PER_U64 > 1)
            data->window_pos -= (NLONG_PER_U64 - 1);
    }

    memcpy(obj->v.bytes,
        &( data->window.l[ data->window_pos ] ),
        sizeof(obj->v.bytes));

    /* addr - (entry_size * (len - pos)) */
    obj->addr = data->addr -
        (sizeof(unsigned long) * (data->window_len - data->window_pos));

    /* Must check each segment aligned to unsigned long (ish)*/
    data->window_pos++;

    set_match_flags(obj, 0);

    return 0;
}


static int
__process_ptrace_next_unaligned(struct process_ctx *ctx,
    struct __process_ptrace_data *data, struct match_object *obj)
{
    int err;
    size_t remaining;

    /* When unaligned, window_pos is a byte offset into window.data[]. */

    remaining = data->window_len - data->window_pos;

    if (remaining < sizeof(uint64_t)) {
        err = get_next_u64(ctx);

        if (err < 0)
            return err;

        if (err == 0) {
            if (remaining > 0)
                goto unaligned;
            return 1;
        }

        data->window_pos -= sizeof(unsigned long) * err;

        /* Different case, slide bytes.
         *
         * Example: (assume long == 32b)
         *
         * Pointing to start of last 32b. The criteria is
         * the position being < 64b from the end.
         *
         * ^ -- window_pos
         *
         * | abcd | efgh | ijkl | mnop | <end>
         *                        ^
         * Shift 8 bytes (64b) [caps are new values]
         *
         * | ijkl | mnop | ABCD | EFGH | <end>
         *          ^
         * This is "fine", but it creats a gap.
         * This starts the next available segment as:
         *   mnop ABCD
         * instead of:
         *   jklm nopA
         *
         * So now we need to shift back by (sizeof(u64) - 1)
         * if space permits.
         */
        remaining = data->window_len - data->window_pos;

        if (remaining >= (sizeof(uint64_t) - 1))
            data->window_pos -= sizeof(uint64_t) - 1;
        else if (remaining > 0)
            data->window_pos = 0;
    }

unaligned:

    remaining = data->window_len - data->window_pos;

    if (remaining >= sizeof(obj->v.bytes)) {
        memcpy(obj->v.bytes,
            &( data->window.data[ data->window_pos ] ),
            sizeof(obj->v.bytes));

        remaining = sizeof(obj->v.bytes);
    }
    else {
        memset(obj->v.bytes, 0, sizeof(obj->v.bytes));

        memcpy(obj->v.bytes,
            &( data->window.data[ data->window_pos ] ),
            remaining);
    }

    /* addr - (window_bytes - pos) */
    obj->addr = data->addr -
        ((sizeof(unsigned long) * data->window_len) - data->window_pos);

    /* Must check byte-by-byte. */
    data->window_pos++;

    set_match_flags(obj, remaining);

    return 0;
}


static int
__process_ptrace_next(struct process_ctx *ctx, struct match_object *obj)
{
    int err;
    size_t remaining;

    if (ctx->data == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (ctx->aligned)
        return __process_ptrace_next_aligned(ctx, ctx->data, obj);

    return __process_ptrace_next_unaligned(ctx, ctx->data, obj);
}


static int
__process_ptrace_set(struct process_ctx *ctx, const struct region *region)
{
    size_t i;
    struct __process_ptrace_data *data;

    if (ctx->data == NULL) {
        errno = EINVAL;
        return -1;
    }


    data = ctx->data;

    data->addr = region->start;
    data->remaining = (size_t)(region->end - region->start);

    data->window_pos = 0;
    data->window_size = 0;


    /* Not enough in the region to pull from.
     * This is not really correct but is is okay.
     * Any mapped space will always be > 16 bytes
     * so there's nothing to worry about. */
    if (data->remaining < sizeof(data->window.data))
        return 1;

    /* Initialize the window. */

    for (i = 0; i < ARRAY_SIZ(data->window.l); ++i) {
        int err;

        err = get_next_segment(ctx);

        if (err < 0)
            return -1;

        /* Not enough space in the region remaining
         * to obtain a segment. Return -1 if we
         * cannot grab the first segment since we
         * need something to search.  After the first
         * just return out (0) so we can process. */
        if (err > 0)
            return (i == 0) ? -1 : 0;
    }

    return 0;
}

static const process_ops __process_ops_ptrace = {
    .init = __process_ptrace_init,
    .fini = __process_ptrace_fini,
    .next = __process_ptrace_next,
    .set = __process_ptrace_set
};


const process_ops *
process_get_ops_ptrace(void)
{
    return &__process_ops_ptrace;
}


/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
