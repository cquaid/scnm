#ifndef H_COMMAND
#define H_COMMAND

#include <stddef.h>

#include "shared/list.h"

/* TODO: Hash Table */

typedef int(*command_fn_t)(size_t, char **);

struct command {
	struct list_head node;

	size_t id;

	command_fn_t handler;

	/* TODO: docs... */
	const char *shortdoc;
	const char *longdoc;

	char name[1];
};

struct command_list {
	struct list_head head;
	size_t next_id;
	size_t size;
};


static inline void
command_list_init(struct command_list *list)
{
	list_head_init(&(list->head));
	list->next_id = 1;
	list->size = 0;
}

extern void command_list_clear(struct command_list *list);

#define command_list_is_empty(command_list) \
	list_is_empty(&((command_list)->head))

#define command_entry(list_node) \
	list_entry(list_node, struct command, node)


extern int register_command(struct command_list *list,
	const char *name, command_fn_t handler,
	const char *shortdoc, const char *longdoc);

extern int exec_line(struct command_list *list, const char *line);

#endif /* H_COMMAND */
