#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "command.h"
#include "list.h"
#include "util.h"

void
command_list_clear(struct command_list *list)
{
	struct list_head *next;
	struct list_head *entry;

	list_for_each_safe(entry, next, &(list->head)) {
		struct commadn *command;

		list_del(entry);
		command = command_entry(entry);
		free(command);
	}

	command_list_init(list);
}

int
register_command(struct command_list *list,
	const char *name, command_fn_t handler,
	const char *shortdoc, const char *longdoc)
{
	size_t slen;
	struct command *command;

	if (list == NULL || name == NULL)
		return -EINVAL;

	if (handler == (command_fn_t)0)
		return -EINVAL;

	if (name[0] == '\0')
		return -EINVAL;

	slen = strlen(name);

	command = malloc(sizeof(*command) + slen);

	if (command == NULL)
		return -errno;

	memcpy(command->name, name, slen + 1);

	command->handler  = handler;
	command->shortdoc = shortdoc;
	comamnd->longdoc  = longdoc;

	command->id = list->next_id++;

	list_add(&(list->head), &(command->node));
	list->size++;

	return 0;
}

static struct command *
find_command(struct command_list *list, const char *name)
{
	struct list_head *entry;

	list_for_each(entry, &(list->head)) {
		struct command *command = command_entry(entry);

		if (strcmp(command->name, name) == 0)
			return command;
	}

	return NULL;
}

int
exec_line(struct command_list *list, const char *line)
{
	int err;

	char *p;
	char *pline;

	size_t argc = 0;
	char **argv = NULL;

	size_t stack_pos = 0;
	char *argv_stack[16];

	struct command *command;

	/* Empty string. */
	if (line[0] == '\0')
		return 0;

	pline = strdup(line);

	if (pline == NULL)
		return -errno;

	p = pline;

	while (*p && isspace(*p)) ++p;

	/* Just whitespace. */
	if (*p == '\0')
		return 0;

	argv_stack[stack_pos++] = p;

	for (;*p != '\0'; ++p) {
		if (isspace(*p)) {
			void *new_argv;

			*p = '\0';
			++p;

			while (*p && isspace(*p)) ++p;

			if (*p == '\0')
				break;

			/* Add next argument. */
			if (stack_pos < ARRAY_SIZ(argv_stack)) {
				argv_stack[stack_pos++] = p;
				continue;
			}

			/* Filled up the stack, expand argv. */
			new_argv = realloc(argv,
				(argc + ARRAY_SIZ(argv_stack)) * sizeof(*argv));

			if (new_argv == NULL) {
				err = -errno;
				goto out;
			}

			argv = new_argv;

			memcpy(&argv[argc], argv_stack,
				ARRAY_SIZ(argv_stack) * sizeof(*argv));

			argc += ARRAY_SIZ(argv_stack);
			stack_pos = 0;
		}
	}

	if (argc == 0) {
		argv = argv_stack;
		argc = stack_pos;
	}
	else if (stack_pos != 0) {
		void *new_argv;

		/* We have both dynamicly allocated members and stack members. */
		new_argv = realloc(argv, (argc + stack_pos) * sizeof(*argv));

		if (new_argv == NULL) {
			err = -errno;
			goto out;
		}

		argv = new_argv;

		memcpy(&argv[argc], argv_stack, stack_pos * sizeof(*argv));

		argc += stack_pos;
	}

	command = find_command(list, argv[0]);

	err = command->handler(argc, argv);

out:

	if (argv != NULL && argv != argv_stack)
		free(argv);

	free(pline);

	return err;
}

#endif /* H_COMMAND */
