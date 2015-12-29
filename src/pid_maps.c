#include <sys/types.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pid_maps.h"
#include "region.h"

/* Format:
 *
 *  address        perms          offset    dev              inode    pathname
 *  -------------  -------------  --------  ---------------  -------  --------
 *  <start>-<end>  <r><w><x><p>   <offset>  <major>:<minor>  <inode>  <path>
 *
 *  address:
 *
 *    start    unsigned long, map start address
 *    end      unsigned long, map end address
 *
 *  perms:
 *
 *    r        byte, read permission or '-' for no read
 *    w        byte, write permissions or '-' for no write
 *    x        byte, exec permissions or '-' for no exec
 *    p        byte, private mapping or 's' for non-copy-on-write (shared)
 *
 *  offset:
 *
 *    offset   unsigned long, offset into the file if from a file
 *
 *  dev:
 *
 *    major    unsigned int, major device number
 *    minor    unsigned int, minor device number
 *
 *  inode:
 *
 *    inode    unsigned long, file inode number if from a file
 *
 *  pathaname:
 *
 *    path     string, path to the file mapped from, psuedo-path, or nothing
 *
 * Possible pseudo-paths:
 *
 *   [heap]          process heap
 *   [stack]         main thread stack
 *   [stack:<tid>]   thread stack
 *   [vdso]          virtual dynamic linked shared object
 *   [vsyscall]      virtual system call mapping
 *   [vvar]          vDSO variables
 */

struct mapping {
	struct {
		unsigned long start;
		unsigned long end;
	} address;

	struct {
		unsigned char read;
		unsigned char write;
		unsigned char exec;
		unsigned char cow;
	} perms;

	unsigned long offset;

	struct {
		unsigned int major;
		unsigned int minor;
	} dev;

	unsigned long inode;

	char pathname[1];
};

static inline struct region *
new_region_from_mapping(const struct mapping *mapping)
{
	size_t len;
	size_t slen;
	struct region *ret;

	slen = strlen(mapping->pathname);
	len = sizeof(*ret) + slen;

	ret = malloc(len);

	if (ret == NULL)
		return NULL;

	ret->id = 0; /* Temporary */

	ret->start = mapping->address.start;
	ret->end = mapping->address.end;

	ret->perms.read  = (mapping->perms.read == 'r');
	ret->perms.write = (mapping->perms.write == 'w');
	ret->perms.exec  = (mapping->perms.exec == 'x');

	ret->perms.private = (mapping->perms.cow == 'p');
	ret->perms.shared  = (mapping->perms.cow == 's');

	memcpy(ret->pathname, mapping->pathname, slen + 1);

	return ret;
}


static inline int
parse_line(const char *line, struct mapping *mapping)
{
	int ret;

	ret = sscanf(line,
			"%lx-%lx %c%c%c%c %lx %x:%x %lu %s",
			&(mapping->address.start), &(mapping->address.end),
			&(mapping->perms.read), &(mapping->perms.write),
			&(mapping->perms.exec), &(mapping->perms.cow),
			&(mapping->offset),
			&(mapping->dev.major), &(mapping->dev.minor),
			&(mapping->inode),
			mapping->pathname);

	if (ret >= 10)
		return 0;

	if (ret == 0)
		return EOF;

	return ret;
}


int
process_pid_maps(pid_t pid, struct region_list *list)
{
	int err;
	int ret = -1;

	char maps_path[64];
	FILE *maps_file = NULL;

	char *line = NULL;
	size_t line_len = 0;

	struct mapping *mapping = NULL;
	size_t mapping_len = 0;

	/* Create path name. */
	err = snprintf(maps_path, sizeof(maps_path),
				"/proc/%u/maps", (unsigned int)pid);

	if (err >= (int)sizeof(maps_path)) {
		errno = E2BIG;
		err = -1;
	}

	if (err < 0) {
		fprintf(stderr, "snprintf(): (%d) %s\n",
			errno, strerror(errno));
		return ret;
	}

	/* Open maps file. */
	maps_file = fopen(maps_path, "r");

	if (maps_file == NULL) {
		fprintf(stderr, "fopen(%s): (%d) %s\n",
			maps_path, errno, strerror(errno));
		return ret;
	}

	region_list_init(list);

	while (getline(&line, &line_len, maps_file) != -1) {
		size_t new_size;
		struct region *region;

		/* Resize if necessary. */
		new_size = sizeof(*mapping) + line_len;

		if (new_size > mapping_len) {
			/* Not using realloc() to avoid additional data move. */
			free(mapping);

			mapping = malloc(mapping, new_size);

			if (mapping == NULL) {
				fprintf(stderr, "realloc(%zd): (%d) %s\n",
					new_size, errno, strerror(errno));
				goto out;
			}

			mapping_len = new_size;
		}

		/* Clear the file name. */
		memset(mapping->pathname, 0, mapping_len - sizeof(*mapping) + 1);

		/* Parse a line from the maps file. */
		err = parse_line(line, mapping);

		if (err < 0) {
			fprintf(stderr, "snprintf(): (%d) %s\n",
				errno, strerror(errno));
			goto out;
		}

		/* Skip if not read and write. */
		if ((mapping->perms.w != 'w') || (mapping->perms.r != 'r'))
			continue;

		fprintf(stderr,
			"%lx-%lx %c%c%c%c %lx %02x:%02x %lu    %s\n",
			mapping->address.start, mapping->address.end,
			mapping->perms.read, mapping->perms.write,
			mapping->perms.exec, mapping->perms.cow,
			mapping->offset,
			mapping->dev.major, mapping->dev.minor,
			mapping->inode,
			mapping->pathname);

		/* Allocate a new region. */
		region = new_region_from_mapping(mapping);

		if (region == NULL) {
			fprintf(stderr, "malloc(): (%d) %s\n",
				errno, strerror(errno));
			goto out;
		}

		region_list_add(list, region);
	}

	ret = 0;

out:

	if (ret != 0)
		region_list_clear(list);

	if (mapping != NULL)
		free(mapping);

	if (line != NULL)
		free(line);

	fclose(maps_file);

	return ret;
}
