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

static ssize_t
__ptrace_peektext(int fd, pid_t pid, void *buf,
    size_t size, unsigned long addr)
{
    int err;
    size_t i;
    size_t rem;
    size_t count;
    unsigned long val;
    (void)fd;

    count = size / sizeof(unsigned long);
    rem = size % sizeof(unsigned long);

    for (i = 0; i < count; ++i) {
        err = ptrace_peektext(pid, addr, &val);

        if (err != 0)
            return -1;

        *(unsigned long *)buf = val;
        buf += sizeof(unsigned long);
        addr += sizeof(unsigned long);
    }

    if (rem != 0) {
        err = ptrace_peektext(pid, addr, &val);

        if (err != 0)
            return -1;

        memcpy(buf, &val, rem);
    }

    return 0;
}

static inline int
get_match_object(struct match_object *obj, read_fn read_actor,
    int fd, pid_t pid, unsigned long addr)
{
    int neg;
    ssize_t err;

    memset(obj, 0, sizeof(*obj));

    err = read_actor(fd, pid, obj->v.bytes, sizeof(obj->v.bytes), addr);

    if (err < 0)
        return -1;

    /* On 0, assume we got enough data (ptrace case) */
    if (err == 0)
        err = sizeof(obj->v.bytes);

    /* Set address */
    obj->addr = addr;

    neg = (obj->v.i64 < 0LL);

    /* Set integer and floating flags. */

    if (obj->v.u64 <= UINT8_MAX) {
        if (neg)
            obj->flags.i8 = !(obj->v.i64 < INT8_MIN);
        else
            obj->flags.i8 = 1;
    }

    /* < 2 bytes means we can't fit int16 or above. */
    if (err < 2)
        return 0;

    if (obj->v.u64 <= UINT16_MAX) {
        if (neg)
            obj->flags.i16 = !(obj->v.i64 < INT16_MIN);
        else
            obj->flags.i16 = 1;
    }

    /* < 4 bytes means we can't fit int32 or above. */
    if (err < 4)
        return 0;

    if (obj->v.u64 <= UINT32_MAX) {
        if (neg)
            obj->flags.i32 = !(obj->v.i64 < INT32_MIN);
        else
            obj->flags.i32 = 1;
    }

    /* No clue how to determine if a valid float32. */
    obj->flags.f32 = 1;

    /* < 8 bytes means we can't fit int64 and double. */
    if (err < 8)
        return 0;

    obj->flags.i64 = 1;
    /* No clue how to determine if a valid float64. */
    obj->flags.f64 = 1;

    return 0;
}


struct __process_ptrace_data {
    union {
        unsigned long l[2];
        char *data[sizeof(unsigned long) * 2];
    } window;

    size_t window_pos;
    size_t window_size;

    unsigned long addr;
    size_t remaining;
};


static int
__process_ptrace_init(struct process_ctx *ctx, int fd, pid_t pid)
{
    /* Don't memset. We don't want to overwrite the ops. */

    ctx->fd = fd;
    ctx->pid = pid;

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

static int
__process_ptrace_next(struct process_ctx *ctx, int aligned,
    struct match_object *obj)
{
    struct __process_ptrace_data *data;

    if (ctx->data == NULL) {
        errno = EINVAL;
        return -1;
    }

    data = ctx->data;

    if (aligned) {

    }


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


    /* Not enough in the region to pull from. */
    if (data->remaining < sizeof(unsigned long))
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
