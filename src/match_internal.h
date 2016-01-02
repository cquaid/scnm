#ifndef H_MATCH_INTERNAL
#define H_MATCH_INTERNAL

#include <stdbool.h>
#include <stdint.h>

#include "shared/list.h"
#include "match.h"

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

#endif /* H_MATCH_INTERNAL */

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
