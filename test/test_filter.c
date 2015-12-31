#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shared/list.h"
#include "pid_maps.h"
#include "region.h"

#define T_INVERT 0x1
#define T_REGEX  0x2
#define T_BASE   0x4
#define T_PATH   0x8

int
main(int argc, char *argv[])
{
	int err;
	pid_t pid;

	struct list_head *entry;
	struct region_list list;
	struct region_filter_list *filter_list;

	int opt;
	int type = 0;
	int count;
	const char *arg;

	struct region_filter_list *(*actor)(struct region_list *, const char *);

	while ((opt = getopt(argc, argv, "nr:b:p:")) != -1) {
		switch (opt) {
		case 'n':
			type |= T_INVERT;
			break;

		case 'r':
			type |= T_REGEX;
			arg = optarg;
			break;

		case 'b':
			type |= T_BASE;
			arg = optarg;
			break;

		case 'p':
			type |= T_PATH;
			arg = optarg;
			break;

		default:
			exit(EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		pid = getpid();
	else
		pid = (pid_t)atoi(argv[1]);

	count = !!(type & T_REGEX)
		  + !!(type & T_BASE)
		  + !!(type & T_PATH);

	if (count > 1) {
		fprintf(stderr, "Only one of -r, -b, -p\n");
		exit(EXIT_FAILURE);
	}

	if (count == 0) {
		fprintf(stderr, "Missing one of -r, -b, -p\n");
		exit(EXIT_FAILURE);
	}


	region_list_init(&list);

	err = process_pid_maps(pid, &list);

	if (err != 0) {
		fprintf(stderr, "Failed to process /proc/%u/maps\n",
			(unsigned int)pid);
		return 1;
	}

	if (type & T_INVERT) {
		if (type & T_BASE)
			actor = region_list_filter_out_basename;
		else if (type & T_PATH)
			actor = region_list_filter_out_pathname;
		else
			actor = region_list_filter_out_regex;
	}
	else {
		if (type & T_BASE)
			actor = region_list_filter_basename;
		else if (type & T_PATH)
			actor = region_list_filter_pathname;
		else
			actor = region_list_filter_regex;
	}

	filter_list = actor(&list, arg);

	if (filter_list == NULL) {
		printf("No matches\n");
		goto out;
	}

	printf("Performing %s%s filtering on ``%s'':\n",
		(type & T_INVERT) ? "inverse " : "",
		(type & T_BASE) ? "basename" : (type & T_PATH) ? "pathname" : "regex",
		arg);

	list_for_each(entry, &(filter_list->head)) {
		char cow;
		struct region *region;
		struct region_filter *filter;

		filter = region_filter_entry(entry);
		region = filter->region;

		if (region->perms.private && region->perms.shared)
			cow = '?';
		else if (region->perms.private)
			cow = 'p';
		else if (region->perms.shared)
			cow = 's';
		else
			cow = '-';

		printf("[%zu] %lx-%lx %c%c%c%c %s\n",
			region->id,
			region->start, region->end,
			(region->perms.read) ? 'r' : '-',
			(region->perms.write) ? 'w' : '-',
			(region->perms.exec) ? 'x' : '-',
			cow,
			region->pathname);
	}

	region_filter_list_destroy(filter_list);

out:

	region_list_clear(&list);

	return 0;
}
