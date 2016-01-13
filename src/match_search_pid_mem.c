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

static int
__process_pid_mem_init(struct process_ctx *ctx, int fd,
    pid_t pid, int aligned)
{
    ctx->fd = fd;
    ctx->pid = pid;
    ctx->aligned = aligned;

    ctx->data = NULL;

    return 0;
}

static void
__process_pid_mem_fini(struct process_ctx *ctx)
{
    if (ctx->data != NULL) {
        free(ctx->data);
        ctx->data = NULL;
    }
}

static int
__process_pid_mem_next(struct process_ctx *ctx, struct match_object *obj)
{
    (void)ctx; (void)obj;
    return -1;
}

static int
__process_pid_mem_set(struct process_ctx *ctx, const struct region *region)
{
    (void)ctx; (void)region;
    return -1;
}

static const struct process_ops __process_ops_pid_mem = {
    .init = __process_pid_mem_init,
    .fini = __process_pid_mem_fini,
    .next = __process_pid_mem_next,
    .set = __process_pid_mem_set
};


const struct process_ops *
process_get_ops_pid_mem(void)
{
    return &__process_ops_pid_mem;
}


/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
