#include <sys/types.h>

#include <libgen.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "list.h"
#include "region.h"

void
region_list_clear(struct region_list *list)
{
	struct list_head *next;
	struct list_head *entry;

	list_for_each_safe(entry, next, &(list->head)) {
		struct region *region;

		list_del(entry);
		region = region_entry(entry);
		free(region);
	}

	region_list_init(list);
}


struct region *
region_list_find_id(struct region_list *list, size_t id)
{
	struct list_head *entry;

	if (region_list_is_empty(list))
		return NULL;

	list_for_each(entry, &(list->head)) {
		struct region *region = region_entry(entry);

		if (region->id == id)
			return region;
	}

	return NULL;
}

struct region *
region_list_find_address(struct region_list *list, unsigned long address)
{
	struct list_head *entry;

	if (region_list_is_empty(list))
		return NULL;

	list_for_each(entry, &(list->head)) {
		struct region *region = region_entry(entry);

		if ((address >= region->start) && (address <= region->end))
			return region;
	}

	return NULL;
}


static inline void
region_filter_list_init(struct region_filter_list *list)
{
	list_head_init(&(list->head));
	list->size = 0;
}

static inline void
region_filter_list_add(struct region_filter_list *list,
	struct region_filter *entry)
{
	list_add_tail(&(entry->node), &(list->head));
	list->size++;
}

#define region_filter_list_is_empty(filter_list) \
	list_is_empty(&((filter_list)->head))

#define region_filter_entry(list_node) \
	list_entry(list_node, struct region_filter, node)

static inline void
region_filter_list_clear(struct region_filter_list *list)
{
	struct list_head *next;
	struct list_head *entry;

	list_for_each_safe(entry, next, &(list->head)) {
		struct region_filter *filter;

		list_del(entry);
		filter = region_filter_entry(entry);
		free(filter);
	}

	region_filter_list_init(list);
}

static inline struct region_filter *
region_filter_new(struct region *region)
{
	struct region_filter *ret;

	ret = malloc(sizeof(*ret));

	if (ret == NULL)
		return NULL;

	ret->region = region;
	return ret;
}

typedef int(*filter_match_fn_t)(struct region *, void *);

static struct region_filter_list *
__region_list_filter(struct region_list *list,
	filter_match_fn_t match_fn, void *data,
	int invert)
{
	int err;

	struct list_head *entry;
	struct region_filter_list *ret;

	/* Make sure there's something to filter. */
	if (region_list_is_empty(list))
		return NULL;

	/* Allocate return filter list. */
	ret = malloc(sizeof(*ret));

	if (ret == NULL)
		return NULL;

	region_filter_list_init(ret);

	/* Loop over each region. */
	list_for_each(entry, &(list->head)) {
		struct region_filter *filter;
		struct region *region = region_entry(entry);

		err = match_fn(region, data);

		if (err < 0)
			goto err;

		if (invert) {
			if (err == 0)
				continue;
		}
		else {
			if (err > 0)
				continue;
		}

		filter = region_filter_new(region);

		if (filter == NULL)
			goto err;

		region_filter_list_add(ret, filter);
	}

	if (region_filter_list_is_empty(ret)) {
		free(ret);
		return NULL;
	}

	return ret;

err:

	region_filter_list_clear(ret);
	free(ret);
	return NULL;
}


static inline struct region_filter_list *
region_list_filter(struct region_list *list,
	filter_match_fn_t match_fn, void *data)
{
	return __region_list_filter(list, match_fn, data, 0);
}

static inline struct region_filter_list *
region_list_filter_out(struct region_list *list,
	filter_match_fn_t match_fn, void *data)
{
	return __region_list_filter(list, match_fn, data, 1);
}


static int
regex_match(struct region *region, void *data)
{
	regmatch_t match;
	const regex_t *regex = (const regex_t *)data;
	return regexec(regex, region->pathname, 1, &match, 0);
}

struct region_filter_list *
region_list_filter_regex(struct region_list *list, const regex_t *regex)
{
	return region_list_filter(list, regex_match, (void *)regex);
}

struct region_filter_list *
region_list_filter_out_regex(struct region_list *list, const regex_t *regex)
{
	return region_list_filter_out(list, regex_match, (void *)regex);
}


static int
pathname_match(struct region *region, void *data)
{
	const char *pathname = (const char *)data;
	return (strcmp(region->pathname, pathname) != 0);
}

struct region_filter_list *
region_list_filter_pathname(struct region_list *list, const char *name)
{
	return region_list_filter(list, pathname_match, (void *)name);
}

struct region_filter_list *
region_list_filter_out_pathname(struct region_list *list, const char *name)
{
	return region_list_filter_out(list, pathname_match, (void *)name);
}


struct basename_match_data {
	const char *find;
	char *path;
	size_t path_len;
};

static int
basename_match(struct region *region, void *data)
{
	size_t slen;
	char *base;
	struct basename_match_data *bmd = data;

	slen = strlen(region->pathname);

	if (slen > bmd->path_len) {
		free(bmd->path);

		bmd->path = malloc(slen + 1);

		if (bmd->path == NULL)
			return -1;

		bmd->path_len = slen;
	}

	memcpy(bmd->path, region->pathname, slen + 1);

	base = basename(bmd->path);

	return (strcmp(base, bmd->find) != 0);
}

struct region_filter_list *
region_list_filter_basename(struct region_list *list, const char *name)
{
	struct region_filter_list *ret;

	struct basename_match_data data = {
		.find = name,
		.path = NULL,
		.path_len = 0
	};

	ret = region_list_filter(list, basename_match, (void *)&data);

	if (data.path != NULL)
		free(data.path);

	return ret;
}

struct region_filter_list *
region_list_filter_out_basename(struct region_list *list, const char *name)
{
	struct region_filter_list *ret;

	struct basename_match_data data = {
		.find = name,
		.path = NULL,
		.path_len = 0
	};

	ret = region_list_filter_out(list, basename_match, (void *)&data);

	if (data.path != NULL)
		free(data.path);

	return ret;
}
