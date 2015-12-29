#ifndef H_REGION
#define H_REGION

#include <regex.h>

#include "list.h"

struct region {
	struct list_head node;

	size_t id;

	unsigned long start;
	unsigned long end;

	struct {
		unsigned int read    : 1;
		unsigned int write   : 1;
		unsigned int exec    : 1;
		unsigned int private : 1;
		unsigned int shared  : 1;
	} perms;

	char pathname[1];
};

struct region_list {
	struct list_head head;
	size_t next_id;
	size_t size;
};

struct region_filter {
	struct list_head node;
	struct region *region;
};

struct region_filter_list {
	struct list_head head;
	size_t size;
};

static inline void
region_list_init(struct region_list *list)
{
	list_head_init(&(list->head));

	list->next_id = 1;
	list->size = 0;
}

#define region_list_is_empty(region_list) \
	list_is_empty(&((region_list)->head))

static inline void
__region_list_add(struct region_list *list, struct region *entry)
{
	list_add_tail(&(entry->node), &(list->head));
	list->size++;
}

static inline void
region_list_add(struct region_list *list, struct region *entry)
{
	/* Assign a new ID. */
	entry->id = list->next_id++;
	__region_list_add(list, entry);
}

static inline void
region_list_del(struct region_list *list, struct region *entry)
{
	list_del(&(entry->node));
	list->size--;
}

extern void region_list_clear(struct region_list *list);

#define region_entry(list_node) \
	list_entry(list_node, struct region, node)

extern struct region *
region_list_find_id(struct region_list *list, size_t id);

extern struct region *
region_list_find_address(struct region_list *list, unsigned long address);

extern struct region_filter_list *
region_list_filter_pathname(struct region_list *list, const char *name);

extern struct region_filter_list *
region_list_filter_out_pathname(struct region_list *list, const char *name);

extern struct region_filter_list *
region_list_filter_basename(struct region_list *list, const char *name);

extern struct region_filter_list *
region_list_filter_out_basename(struct region_list *list, const char *name);

extern struct region_filter_list *
region_list_filter_regex(struct region_list *list, const regex_t *regex);

extern struct region_filter_list *
region_list_filter_out_regex(struct region_list *list, const regex_t *regex);

#endif /* H_REGION */
