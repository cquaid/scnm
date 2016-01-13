#ifndef H_MATCH_INTERNAL
#define H_MATCH_INTERNAL

#include <sys/types.h>

#include <stdbool.h>
#include <stdint.h>

#include "shared/list.h"
#include "match.h"
#include "region.h"

#define match_chunk_entry(list_node) \
    list_entry(list_node, struct match_chunk_header, node)

static inline void
match_list_add(struct match_list *list, struct match_chunk_header *entry)
{
    list_add_tail(&(entry->node), &(list->head));
    list->size++;
}

static inline void
match_list_del(struct match_list *list, struct match_chunk_header *entry)
{
    list_del(&(entry->node));
    list->size--;
}

/* Scan / Search types */

struct process_ctx;

typedef int(*process_init_fn)(struct process_ctx *, int fd, pid_t pid, int);
typedef void(*process_fini_fn)(struct process_ctx *);
typedef int(*process_next_fn)(struct process_ctx *, struct match_object *);
typedef int(*process_set_fn)(struct process_ctx *, const struct region *);

struct process_ops {
    process_init_fn init;
    process_fini_fn fini;
    process_next_fn next;
    process_set_fn set;
};

struct process_ctx {
    int fd;
    pid_t pid;
    int aligned;
    void *data;
    const struct process_ops *ops;
};

extern const struct process_ops * process_get_ops_pid_mem(void);
extern const struct process_ops * process_get_ops_ptrace(void);

extern void set_match_flags(struct match_object *obj, size_t len);

#endif /* H_MATCH_INTERNAL */

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
