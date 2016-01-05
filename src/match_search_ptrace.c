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
 * TODO: bytes at the end of a region... and set_match_flags needs len param.
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

static inline int
get_next_u64(struct process_ctx *ctx)
{
    int err;
    size_t i;

    for (i = 0; i < NLONG_PER_U64; ++i) {
        err = get_next_segment(ctx);

        if (err != 0)
            return err;
    }

    return 0;
}

static int
__process_ptrace_next(struct process_ctx *ctx, struct match_object *obj)
{
    int err;
    struct __process_ptrace_data *data;

    if (ctx->data == NULL) {
        errno = EINVAL;
        return -1;
    }

    data = ctx->data;

    /* When aligned, window_pos is used as an index into window.l[]. */

    if (ctx->aligned) {
        if (data->window_pos >= ARRAY_SIZ(data->window.l)) {
            err = get_next_u64(ctx);

            if (err != 0)
                return err;

            data->window_pos = ARRAY_SIZ(data->window.l) - i;
        }

        memcpy(&(obj->v.bytes),
            &( data->window.l[ data->window_pos ] ),
            sizeof(obj->v.bytes));

        /* addr - (entry_size * (len - pos))*/
        obj->addr = data->addr -
            (sizeof(unsigned long) * (data->window_len - data->window_pos));

        /* Must check each segment aligned to unsigned long (ish)*/
        data->window_pos++;

        set_match_flags(obj);

        return 0;
    }

    /* When unaligned, window_pos is a byte offset into window.data[]. */

    if ((data->window_len - data->window_pos) < sizeof(uint64_t)) {
        err = get_next_u64(ctx);

        if (err != 0)
            return err;

        data->window_pos -= sizeof(uint64_t);
    }

    memcpy(&(obj->v.bytes),
        &( data->window.data[ data->window_pos ] ),
        sizeof(obj->v.bytes));

    /* addr - (window_bytes - pos) */
    obj->addr = data->addr -
        ((sizeof(unsigned long) * data->window_len) - data->window_pos);

    /* Must check byte-by-byte. */
    data->window_pos++;

    set_match_flags(obj);

    return 0;
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
