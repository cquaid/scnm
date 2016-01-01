#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "pid_mem.h"

static inline int
get_mem_path(pid_t pid, char *buf, size_t size)
{
    int err;

    err = snprintf(buf, size, "/proc/%u/mem",
                (unsigned int)pid);

    if (err < 0)
        return err;

    if ((size_t)err >= size) {
        errno = ERANGE; /* EOVERFLOW? */
        return -1;
    }

    return 0;
}

static inline int
access_wrapper(pid_t pid, int mode)
{
    int err;
    char mem_path[64];

    /* This should never fail. */
    err = get_mem_path(pid, mem_path, sizeof(mem_path));

    if (err < 0)
        return err;

    return access(mem_path, mode);
}


/**
 * Determine if the caller has access to read the
 * /proc/<pid>/mem file of the given process id.
 *
 * @param[in] pid - process id to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
can_read_pid_mem(pid_t pid)
{
    return access_wrapper(pid, R_OK);
}


/**
 * Determine if the caller has access to write to
 * the /proc/<pid>/mem file of the given process id.
 *
 * @param[in] pid - process id to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
can_write_pid_mem(pid_t pid)
{
    return access_wrapper(pid, W_OK);
}


/* Used by open_pid_mem to decode PID_MEM_FLAGS_*. */
static const int open_flags_from_pid_mem_flags[] = {
    [PID_MEM_FLAGS_READ] = O_RDONLY,
    [PID_MEM_FLAGS_WRITE] = O_WRONLY,
    [PID_MEM_FLAGS_READ | PID_MEM_FLAGS_WRITE] = O_RDWR
};


/**
 * Open /proc/<pid>/mem with the requested flags.
 *
 * @param[in] pid - process id to open /proc/pid/mem for
 * @param[in] pid_mem_flags - pid mem open flags
 *
 * @return >= 0 on success (valid file descriptor)
 * @return < 0 on failure with error returned in errno
 */
int
open_pid_mem(pid_t pid, int pid_mem_flags)
{
    int err;
    int real_flags;
    char mem_path[64];

    /* Validate flags */

    if (pid_mem_flags == 0) {
        errno = EINVAL;
        return -1;
    }

    if ((pid_mem_flags & ~PID_MEM_FLAGS_MASK) != 0) {
        errno = EINVAL;
        return -1;
    }

    /* This should never fail. */
    err = get_mem_path(pid, mem_path, sizeof(mem_path));

    if (err < 0)
        return err;

    /* Decode flags */
    real_flags = open_flags_from_pid_mem_flags[ pid_mem_flags ];

    return open(mem_path, real_flags);
}


/**
 * Read data from /proc/<pid>/mem.
 *
 * @param[in] pid - process id of the mem file to read
 * @param[out] buf - storage location for read data
 * @param[in] size - size to read
 * @param[in] offset - offset to read from
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes read into buf)
 */
ssize_t
read_pid_mem(pid_t pid, void *buf, size_t size, off_t offset)
{
    int fd;
    int oerrno;
    ssize_t len;

    fd = open_pid_mem(pid, PID_MEM_FLAGS_READ);

    if (fd < 0)
        return -1;

    len = pread(fd, buf, size, offset);

    /* Store original errno. */
    if (len < 0)
        oerrno = errno;

    /* Ignore close errors. */
    (void)close_pid_mem(fd);

    if (len < 0)
        errno = oerrno;

    return len;
}


/**
 * Dumb wrapper for reading data from /proc/<pid>/mem
 * assuming that is the open file.
 *
 * @param[in] fd - file descriptor to read from
 * @param[out] buf - storage location for read data
 * @param[in] size - size to read
 * @param[in] offset - offset to read from
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes read into buf)
 */
ssize_t
read_pid_mem_fd(int fd, void *buf, size_t size, off_t offset)
{
    return pread(fd, buf, size, offset);
}


/**
 * Read data from /proc/<pid>/mem.
 *
 * This function guarntees 'size' bytes read into the buffer on
 * a successful return only if EOF was not encountered before
 * finishing.
 *
 * @param[in] pid - process id of the mem file to read
 * @param[out] buf - storage location for read data
 * @param[in] size - size to read
 * @param[in] offset - offset to read from
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes read into buf)
 */
ssize_t
read_pid_mem_loop(pid_t pid, void *buf, size_t size, off_t offset)
{
    int fd;
    int oerrno;
    ssize_t ret;

    fd = open_pid_mem(pid, PID_MEM_FLAGS_READ);

    if (fd < 0)
        return -1;

    ret = read_pid_mem_loop_fd(fd, buf, size, offset);

    /* Store original errno. */
    if (ret < 0)
        oerrno = errno;

    /* Ignore close errors. */
    (void)close_pid_mem(fd);

    if (ret < 0)
        errno = oerrno;

    return ret;
}


/**
 * Read data from /proc/<pid>/mem assuming that is the open file.
 *
 * This function guarntees 'size' bytes read into the buffer on
 * a successful return only if EOF was not encountered before
 * finishing.
 *
 * @param[in] fd - file descriptor to read from
 * @param[out] buf - storage location for read data
 * @param[in] size - size to read
 * @param[in] offset - offset to read from
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes read into buf)
 */
ssize_t
read_pid_mem_loop_fd(int fd, void *buf, size_t size, off_t offset)
{
    off_t err;
    char *pbuf;
    ssize_t remaining;

    err = lseek(fd, offset, SEEK_SET);

    if (err == (off_t)-1)
        return (ssize_t)-1;

    pbuf = (void *)buf;
    remaining = (ssize_t)size;

    while (remaining) {
        ssize_t len;

        len = read(fd, pbuf, remaining);

        if (len < 0)
            return len;

        if (len == 0)
            return ((ssize_t)size) - remaining;
        
        pbuf += len;
        remaining -= len;
    }

    return (ssize_t)size;
}


/**
 * Write data to /proc/<pid>/mem.
 *
 * @param[in] pid - process id of the mem file to write to
 * @param[in] buf - data to write
 * @param[in] size - size to write
 * @param[in] offset - offset to write to
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes written from buf)
 */
ssize_t
write_pid_mem(pid_t pid, void *buf, size_t size, off_t offset)
{
    int fd;
    int oerrno;
    ssize_t len;

    fd = open_pid_mem(pid, PID_MEM_FLAGS_WRITE);

    if (fd < 0)
        return -1;

    len = pwrite(fd, buf, size, offset);

    /* Store original errno. */
    if (len < 0)
        oerrno = errno;

    /* Ignore close errors. */
    (void)close_pid_mem(fd);

    if (len < 0)
        errno = oerrno;

    return len;
}


/**
 * Dumb wrapper for writing data to /proc/<pid>/mem
 * assuming that is the open file.
 *
 * @param[in] fd - file descriptor to write to
 * @param[in] buf - data to write
 * @param[in] size - size to write
 * @param[in] offset - offset to write to
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes written from buf)
 */
ssize_t
write_pid_mem_fd(int fd, void *buf, size_t size, off_t offset)
{
    return pwrite(fd, buf, size, offset);
}


/**
 * Write data to /proc/<pid>/mem.
 *
 * This function guarntees 'size' bytes written from the buffer on
 * a successful return only if EOF was not encountered before
 * finishing.
 *
 * @param[in] pid - process id of the mem file to write to
 * @param[in] buf - data to write
 * @param[in] size - size to write
 * @param[in] offset - offset to write to
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes written from buf)
 */
ssize_t
write_pid_mem_loop(pid_t pid, void *buf, size_t size, off_t offset)
{
    int fd;
    int oerrno;
    ssize_t ret;

    fd = open_pid_mem(pid, PID_MEM_FLAGS_WRITE);

    if (fd < 0)
        return -1;

    ret = write_pid_mem_loop_fd(fd, buf, size, offset);

    /* Store original errno. */
    if (ret < 0)
        oerrno = errno;

    /* Ignore close errors. */
    (void)close_pid_mem(fd);

    if (ret < 0)
        errno = oerrno;

    return ret;
}


/**
 * Write data to /proc/<pid>/mem assuming that is the oppen file.
 *
 * This function guarntees 'size' bytes written from the buffer on
 * a successful return only if EOF was not encountered before
 * finishing.
 *
 * @param[in] pid - file descriptor to write to
 * @param[in] buf - data to write
 * @param[in] size - size to write
 * @param[in] offset - offset to write to
 *
 * @return < 0 on failure with error stored in errno
 * @return 0 on end of file
 * @return > 0 on success (number of bytes written from buf)
 */
ssize_t
write_pid_mem_loop_fd(int fd, void *buf, size_t size, off_t offset)
{
    off_t err;
    char *pbuf;
    ssize_t remaining;

    err = lseek(fd, offset, SEEK_SET);

    if (err == (off_t)-1)
        return (ssize_t)-1;

    pbuf = (void *)buf;
    remaining = (ssize_t)size;

    while (remaining) {
        ssize_t len;

        len = write(fd, pbuf, remaining);

        if (len < 0)
            return len;

        if (len == 0)
            return ((ssize_t)size) - remaining;
        
        pbuf += len;
        remaining -= len;
    }

    return (ssize_t)size;
}


/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
