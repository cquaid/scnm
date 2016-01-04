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
#include "pid_mem.h"
#include "region.h"


/**
 * @file match_search_pid_mem.c
 *
 * Memory searching callback routines using /proc/<pid>/mem
 *
 * TODO: create and supply a wintermute context holding a ptracer context.
 */

typedef int(*search_match_fn)(const struct match_object *,
    const struct match_needle *, const struct match_needle *);

static ssize_t
__read_pid_mem(int fd, pid_t pid, void *buf,
    size_t size, unsigned long addr)
{
    ssize_t err;
    (void)pid;

    err = read_pid_mem_loop_fd(fd, buf, size, (off_t)addr);

    if (err < 0)
        return -1;

    /* Will return smaller size than asked for if end of a range. */
    return err;
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


static int
__process_pid_mem_init(struct process_ctx *ctx, int fd, pid_t pid)
{
    ctx->fd = fd;
    ctx->pid = pid;

    ctx->data = NULL;

    return 0;
}

static void
__process_pid_mem_fini(struct process_ctx *ctx)
{
}

static int
__process_pid_mem_next(struct process_ctx *ctx)
{
    return 0;
}

static int
__process_pid_mem_set(struct process_ctx *ctx, const struct region *region)
{
    return 0;
}

static const process_ops __process_ops_pid_mem = {
    .init = __process_pid_mem_init,
    .fini = __process_pid_mem_fini,
    .next = __process_pid_mem_next,
    .set = __process_pid_mem_set
};


const process_ops *
process_get_ops_pid_mem(void)
{
    return &__process_ops_pid_mem;
}


/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
