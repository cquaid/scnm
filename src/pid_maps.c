#include <sys/types.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    /* EOF case, just pipe break it... */
    if (ret == 0) {
        errno = EPIPE;
        return -1;
    }

    return ret;
}

static inline int
get_maps_name(pid_t pid, char *buf, size_t size)
{
    int err;

    err = snprintf(buf, size, "/proc/%u/maps",
                (unsigned int)pid);

    if (err < 0)
        return err;

    if ((size_t)err >= size) {
        errno = ERANGE; /* EOVERFLOW? */
        return -1;
    }

    return 0;
}

/**
 * Determine if the caller has access to read the
 * /proc/<pid>/maps file of the given process id.
 *
 * @param[in] pid - process id to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
can_read_pid_maps(pid_t pid)
{
    int err;
    char maps_path[64];

    /* This should never fail. */
    err = get_maps_name(pid, maps_path, sizeof(maps_path));

    if (err < 0)
        return err;

    return access(maps_path, R_OK);
}

/**
 * Parse /proc/<pid>/maps and return a list of
 * accessable memory regions for the process.
 *
 * @param[in] pid - process id to get maps from
 * @param[out] list - initialized region_list to store region info in.
 *
 * @note
 *  At the moment, this function only returns regions
 *  with both read and write permissions set.
 *
 * @return 0 on success
 * @return < 0 on failure with error stored in errno
 */
int
process_pid_maps(pid_t pid, struct region_list *list)
{
    int err;
    int ret = -1;
    int oerrno;

    char maps_path[64];
    FILE *maps_file = NULL;

    char *line = NULL;
    size_t line_len = 0;

    struct mapping *mapping = NULL;
    size_t mapping_len = 0;

    /* This should never fail. */
    err = get_maps_name(pid, maps_path, sizeof(maps_path));

    if (err < 0)
        return ret;

    /* Open maps file. */
    maps_file = fopen(maps_path, "r");

    if (maps_file == NULL)
        return ret;

    region_list_init(list);

    while (getline(&line, &line_len, maps_file) != -1) {
        size_t new_size;
        struct region *region;

        /* Resize if necessary. */
        new_size = sizeof(*mapping) + line_len;

        if (new_size > mapping_len) {
            /* Not using realloc() to avoid additional data move. */
            free(mapping);

            mapping = malloc(new_size);

            if (mapping == NULL)
                goto out;

            mapping_len = new_size;
        }

        /* Clear the file name. */
        memset(mapping->pathname, 0, mapping_len - sizeof(*mapping) + 1);

        /* Parse a line from the maps file. */
        err = parse_line(line, mapping);

        if (err < 0)
            goto out;

        /* Skip if not read and write. */
        if ((mapping->perms.write != 'w') || (mapping->perms.read != 'r'))
            continue;

        /* Allocate a new region. */
        region = new_region_from_mapping(mapping);

        if (region == NULL)
            goto out;

        region_list_add(list, region);
    }

    ret = 0;

out:

    /* Try not to clobber errno. */
    if (ret < 0)
        oerrno = errno;

    if (ret != 0)
        region_list_clear(list);

    if (mapping != NULL)
        free(mapping);

    if (line != NULL)
        free(line);

    fclose(maps_file);

    /* Restore errno. */
    if (ret < 0)
        errno = oerrno;

    return ret;
}

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
