#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shared/list.h"
#include "pid_maps.h"
#include "region.h"

int
main(int argc, char *argv[])
{
	int err;
	pid_t pid;

	struct list_head *entry;
	struct region_list list;

	if (argc == 1)
		pid = getpid();
	else
		pid = (pid_t)atoi(argv[1]);

	region_list_init(&list);

	err = process_pid_maps(pid, &list);

	if (err != 0) {
		fprintf(stderr, "Failed to process /proc/%u/maps\n",
			(unsigned int)pid);
		return 1;
	}

	list_for_each(entry, &(list.head)) {
		char cow;
		struct region *region;

		region = region_entry(entry);

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

	region_list_clear(&list);

	return 0;
}
