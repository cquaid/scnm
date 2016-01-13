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
 * @file match_search.c
 *
 * Memory searching routines
 *
 * TODO: externalize checks for /proc/pid/mem readability.
 * TODO: create and supply a wintermute context holding a ptracer context.
 * TODO: pretty sure some of the match functions are bullshit.
 */

typedef int(*search_match_fn)(const struct match_object *,
    const struct match_needle *, const struct match_needle *);

void
set_match_flags(struct match_object *obj, size_t size)
{
    memset(&(obj->flags), 0, sizeof(obj->flags));

    if (size == 0)
        size = sizeof(uint64_t);

    /* Set integer and floating flags. */

    if (size >= sizeof(uint64_t)) {
        obj->flags.i64 = 1;
        obj->flags.f64 = 1;
    }

    if (size >= sizeof(uint32_t)) {
        obj->flags.i32 = 1;
        obj->flags.f32 = 1;
    }

    if (size >= sizeof(uint16_t))
        obj->flags.i16 = 1;

    obj->flags.i8 = 1;
}

static inline struct match_chunk_header *
new_chunk(unsigned long size)
{
    struct match_chunk_header *ret;

    ret = calloc(1, sizeof(*ret) + ((size - 1) * sizeof(ret->objects[0])));

    if (ret == NULL)
        return NULL;

    ret->count = size;

    return ret;
}


static inline int
process_region(struct process_ctx *ctx,
    struct match_list *list, const struct region *region,
    search_match_fn match, const struct match_needle *needle_1,
    const struct match_needle *needle_2,
    struct match_chunk_header **pcurrent_chunk)
{
    int err;
    struct match_chunk_header *current_chunk = *pcurrent_chunk;

    if (current_chunk != NULL && (current_chunk->used >= current_chunk->count))
        current_chunk = NULL;

    if (current_chunk == NULL) {
        current_chunk = new_chunk(MATCH_CHUNK_SIZE_HUGE);

        if (current_chunk == NULL)
            return -1;

        match_list_add(list, current_chunk);
    }

    err = ctx->ops->set(ctx, region);

    if (err < 0)
        return -1;

    for (;;) {
        struct match_object *obj;

        if (current_chunk->used >= current_chunk->count) {
            current_chunk = new_chunk(MATCH_CHUNK_SIZE_HUGE);

            if (current_chunk == NULL)
                return -1;

            match_list_add(list, current_chunk);
        }

        obj = &( current_chunk->objects[ current_chunk->used ] );

        /* ->next() return values:
         *   0 - success, more to process (obj gathered)
         *   1 - success, no more to process (no obj gathered)
         *  -1 - an error occured
         */
        err = ctx->ops->next(ctx, obj);

        if (err < 0)
            return -1;

        if (err > 0)
            goto out;

        /* Verify match */

        /* match() return values:
         *  0 - did not match criteria
         *  1 - matched criteria
         */
        if (match(obj, needle_1, needle_2))
            current_chunk->used++;
    }

out:

    *pcurrent_chunk = current_chunk;

    return 0;
}


static int
__search(pid_t pid, struct match_list *list,
    const struct match_needle *needle_1,
    const struct match_needle *needle_2,
    const struct region_list *regions,
    int options, search_match_fn match)
{
    int fd;
    int err;
    int ret;
    int oerrno;

    struct process_ctx ctx;
    struct list_head *entry;

    struct match_chunk_header *current_chunk = NULL;


    /* Determine which memory reading method to use. */
    err = can_read_pid_mem(pid);

    if (err == 0) {
        fd = open_pid_mem(pid, PID_MEM_FLAGS_READ);

        /* Even if we have access, if we can't open the file,
         * try to use ptrace instead. */
        if (fd < 0)
            ctx.ops = process_get_ops_ptrace();
        else
            ctx.ops = process_get_ops_pid_mem();
    }
    else {
        ctx.ops = process_get_ops_ptrace();
        fd = -1;
    }

    /* Initialize the processing context. */
    err = ctx.ops->init(&ctx, fd, pid,
            (options & SEARCH_OPT_ALIGNED));

    if (err != 0) {
        ret = -1;
        goto out;
    }

    list_for_each(entry, &(regions->head)) {
        struct region *region;

        region = region_entry(entry);

        err = process_region(&ctx, list,
                region, match, needle_1,
                needle_2, &current_chunk);

        if (err < 0) {
            ret = -1;
            goto out;
        }
    }

out:

    if (ret != 0)
        oerrno = errno;

    /* Finalize the processing context */
    ctx.ops->fini(&ctx);

    /* close the pid_mem fd if open */
    if (fd != -1)
        (void)close_pid_mem(fd);

    if (ret != 0)
        errno = oerrno;

    return ret;
}


static int
__search_eq(const struct match_object *value,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)unused;

    if (needle->obj.flags.i8) {
        if (needle->obj.v.u8 == value->v.u8)
            return 1;
    }

    if (needle->obj.flags.i16) {
        if (needle->obj.v.u16 == value->v.u16)
            return 1;
    }

    if (needle->obj.flags.i32 || needle->obj.flags.f32) {
        if (needle->obj.v.u32 == value->v.u32)
            return 1;
    }

    if (needle->obj.flags.i64 || needle->obj.flags.f64) {
        if (needle->obj.v.u64 == value->v.u64)
            return 1;
    }

   return 0;
}

/**
 * Find matches equal to a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to find
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
search_eq(pid_t pid, struct match_list *list,
    const struct match_needle *needle,
    const struct region_list *regions,
    int options)
{
    return __search(pid, list, needle, NULL, regions, options, __search_eq);
}


/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
