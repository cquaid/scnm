#include <sys/types.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "shared/list.h"
#include "shared/util.h"
#include "ptracer/ptracer.h"

#include "match.h"
#include "match_internal.h"
#include "pid_mem.h"


/**
 * @file match_match.c
 *
 * Match List filtering functions.
 *
 * TODO: externalize checks for /proc/pid/mem readability.
 * TODO: create and supply a wintermute context holding a ptracer context.
 * TODO: pretty sure some of the match functions are bullshit.
 */

typedef int(*match_fn)(const struct match_object *,
    const struct match_object *, const struct match_needle *,
    const struct match_needle *);

typedef ssize_t(*read_fn)(int, pid_t, void *, size_t, unsigned long);

static ssize_t
__read_pid_mem(int fd, pid_t pid, void *buf,
    size_t size, unsigned long addr)
{
    ssize_t err;
    (void)pid;

    err = read_pid_mem_loop_fd(fd, buf, size, (off_t)addr);

    if (err < 0)
        return -1;

    /* Will return smaller size than asked for if end of a range. */
    return err;
}

static ssize_t
__ptrace_peektext(int fd, pid_t pid, void *buf,
    size_t size, unsigned long addr)
{
    int err;
    size_t i;
    size_t rem;
    size_t count;
    unsigned long val;
    (void)fd;

    count = size / sizeof(unsigned long);
    rem = size % sizeof(unsigned long);

    for (i = 0; i < count; ++i) {
        err = ptrace_peektext(pid, addr, &val); 

        if (err != 0)
            return -1;

        *(unsigned long *)buf = val;
        buf += sizeof(unsigned long);
        addr += sizeof(unsigned long);
    }

    if (rem != 0) {
        err = ptrace_peektext(pid, addr, &val);

        if (err != 0)
            return -1;

        memcpy(buf, &val, rem);
    }

    return 0;
}

static inline int
get_match_object(struct match_object *obj, read_fn read_actor,
    int fd, pid_t pid, unsigned long addr)
{
    int neg;
    ssize_t err;

    memset(obj, 0, sizeof(*obj));

    err = read_actor(fd, pid, obj->v.bytes, sizeof(obj->v.bytes), addr);

    if (err < 0)
        return -1;

    /* On 0, assume we got enough data (ptrace case) */
    if (err == 0)
        err = sizeof(obj->v.bytes);

    /* Set address */
    obj->addr = addr;

    neg = (obj->v.i64 < 0LL);

    /* Set integer and floating flags. */

    if (obj->v.u64 <= UINT8_MAX) {
        if (neg)
            obj->flags.i8 = !(obj->v.i64 < INT8_MIN);
        else
            obj->flags.i8 = 1;
    }

    /* < 2 bytes means we can't fit int16 or above. */
    if (err < 2)
        return 0;

    if (obj->v.u64 <= UINT16_MAX) {
        if (neg)
            obj->flags.i16 = !(obj->v.i64 < INT16_MIN);
        else
            obj->flags.i16 = 1;
    }

    /* < 4 bytes means we can't fit int32 or above. */
    if (err < 4)
        return 0;  

    if (obj->v.u64 <= UINT32_MAX) {
        if (neg)
            obj->flags.i32 = !(obj->v.i64 < INT32_MIN);
        else
            obj->flags.i32 = 1;
    }

    /* No clue how to determine if a valid float32. */
    obj->flags.f32 = 1;

    /* < 8 bytes means we can't fit int64 and double. */
    if (err < 8)
        return 0;

    obj->flags.i64 = 1;
    /* No clue how to determine if a valid float64. */
    obj->flags.f64 = 1;

    return 0;
}

static inline void
delete_chunk_object(struct match_chunk_header *chunk, size_t slot)
{
    if (slot >= chunk->used)
        return;

    /* Decrement first to simplif calculations... minorly. */
    chunk->used--;

    if (slot == chunk->used)
        return;

    memcpy(&(chunk->objects[slot]),
        &(chunk->objects[ chunk->used ]),
        sizeof(chunk->objects[0]));
}

#define match_list_delete_entry(list, entry) \
    do { \
        list_del(&((entry)->node)); \
        (list)->size--; \
        free(entry); \
    } while (0)

static int
__match(pid_t pid, struct match_list *list,
    const struct match_needle *needle_1,
    const struct match_needle *needle_2,
    match_fn match)
{
    int fd;
    int err;
    int ret;
    struct list_head *next;
    struct list_head *entry;

    read_fn read_actor;

    struct match_chunk_header *current_chunk = NULL;


    if (match_list_is_empty(list))
        return 0;

    /* Determine which memory reading method to use. */
    err = can_read_pid_mem(pid);

    if (err == 0) {
        fd = open_pid_mem(pid, PID_MEM_FLAGS_READ);

        /* Even if we have access, if we can't open the file,
         * try to use ptrace instead. */
        if (fd < 0)
            read_actor = __ptrace_peektext;
        else
            read_actor = __read_pid_mem;
    }
    else {
        read_actor = __ptrace_peektext;
        fd = -1;
    }

    /* Iterate over each match chunk. */

    list_for_each_safe(entry, next, &(list->head)) {
        size_t i;
        struct match_chunk_header *header;

        header = match_chunk_entry(entry);

        i = 0;
        while (i < header->used) {
            struct match_object tmp;
            struct match_object *obj;

            obj = &(header->objects[i]);

            err = get_match_object(&tmp, read_actor, fd, pid, obj->addr);

            if (err < 0) {
                ret = -1;
                goto out;
            }

            if (!match(obj, &tmp, needle_1, needle_2)) {
                /* Not a match, remove it */
                delete_chunk_object(header, i);
                /* Try the next one, but don't increment position. */
                continue;
            }

            /* Found a match */
            ++i;
        }

        /* Remove emptied chunks. */
        if (header->used == 0)
            match_list_delete_entry(list, header);
    }

    /* Everything has been checked, now consolidate the chunks. */

    list_for_each_safe(entry, next, &(list->head)) {
        size_t current_delta;
        struct match_chunk_header *header;

        header = match_chunk_entry(entry);

        /* Skip over full chunks */
        if (header->used == header->count)
            continue;

        /* Set our chunk if there's space to fit more. */
        if (current_chunk == NULL) {
            current_chunk = header;
            continue;
        }

        /* Can fit entirely in current chunk. */
        current_delta = current_chunk->count - current_chunk->used;

        if (header->used < current_delta) {
            /* Always move into the bigger chunk. */
            if (header->count > current_chunk->count)
                SWAP(header, current_chunk);

            /* Copy the objects over to current_chunk. */

            memcpy(&(current_chunk->objects[ current_chunk->used ]),
                header->objects, sizeof(header->objects[0]) * header->used);

            current_chunk->used += header->used;

            match_list_delete_entry(list, header);

            /* Full, find a new chunk. */
            if (current_chunk->used == current_chunk->count)
                current_chunk = NULL;

            continue;
        }

        /* Cannot entirely fit, so copy over as many as we can. */

        /* Always fill a map. */

        if ((header->count - header->used) < current_delta) {
            SWAP(header, current_chunk);
            /* Recalculate delta from swap  */
            current_delta = current_chunk->count - current_chunk->used;
        }

        /* Copy elements from the END of the header chunk. */
        memcpy(&(current_chunk->objects[ current_chunk->used ]),
            &(header->objects[ header->used - current_delta ]),
            sizeof(header->objects[0]) * current_delta);

        header->used -= current_delta;
        current_chunk->used += current_delta;

        /* current_chunk is full, change to header. */
        current_chunk = header;
    }

    ret = 0;

out:

    if (read_actor == __read_pid_mem) {
        int oerrno = errno;
        (void)close_pid_mem(fd);
        errno = oerrno;
    }

    return ret;
}


static int
__match_eq(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)orig;
    (void)unused;

    /* Find the largest flag for needle and compare. */
    if (needle->obj.flags.i64 || needle->obj.flags.f64)
        return (needle->obj.v.u64 == new->v.u64);

    if (needle->obj.flags.i32 || needle->obj.flags.f32)
        return (needle->obj.v.u32 == new->v.u32);

    if (needle->obj.flags.i16)
        return (needle->obj.v.u16 == new->v.u16);

    if (needle->obj.flags.i8)
        return (needle->obj.v.u8 == new->v.u8);

    return 0;
}

/**
 * Find matches equal to a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to find
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_eq(pid_t pid, struct match_list *list,
    const struct match_needle *needle)
{
    return __match(pid, list, needle, NULL, __match_eq);
}


static int
__match_ne(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)orig;
    (void)unused;

    /* Find the largest flag for needle and compare. */
    if (needle->obj.flags.i64 || needle->obj.flags.f64)
        return (needle->obj.v.u64 != new->v.u64);

    if (needle->obj.flags.i32 || needle->obj.flags.f32)
        return (needle->obj.v.u32 != new->v.u32);

    if (needle->obj.flags.i16)
        return (needle->obj.v.u16 != new->v.u16);

    if (needle->obj.flags.i8)
        return (needle->obj.v.u8 != new->v.u8);

    return 0;
}

/**
 * Find matches not equal to a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to compare against
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_ne(pid_t pid, struct match_list *list,
    const struct match_needle *needle)
{
    return __match(pid, list, needle, NULL, __match_ne);
}


static int
__match_lt(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)orig;
    (void)unused;

    /* This is more difficult an integer and a floating point
     * have different meaning of less than. Since integer
     * and floating point flags should be mutually exclusive,
     * ignore the complexity for now. */

    /* Find the largest flag for needle and compare. */
    if (needle->obj.flags.i64) {
        return (needle->obj.v.u64 < new->v.u64)
            || (needle->obj.v.i64 < new->v.i64);
    }

    if (needle->obj.flags.f64)
        return (needle->obj.v.f64 < new->v.f64);

    if (needle->obj.flags.i32) {
        return (needle->obj.v.u32 < new->v.u32)
            || (needle->obj.v.i32 < new->v.i32);
    }

    if (needle->obj.flags.f32)
        return (needle->obj.v.f32 < new->v.f32);

    if (needle->obj.flags.i16) {
        return (needle->obj.v.u16 < new->v.u16)
            || (needle->obj.v.i16 < new->v.i16);
    }

    if (needle->obj.flags.i8) {
        return (needle->obj.v.u8 < new->v.u8)
            || (needle->obj.v.i8 < new->v.i8);
    }

    return 0;
}

/**
 * Find matches less than a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to compare against
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_lt(pid_t pid, struct match_list *list,
    const struct match_needle *needle)
{
    return __match(pid, list, needle, NULL, __match_lt);
}


static int
__match_le(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)orig;
    (void)unused;

    /* This is more difficult an integer and a floating point
     * have different meaning of less than. Since integer
     * and floating point flags should be mutually exclusive,
     * ignore the complexity for now. */

    /* Find the largest flag for needle and compare. */
    if (needle->obj.flags.i64) {
        return (needle->obj.v.u64 <= new->v.u64)
            || (needle->obj.v.i64 <= new->v.i64);
    }

    if (needle->obj.flags.f64)
        return (needle->obj.v.f64 <= new->v.f64);

    if (needle->obj.flags.i32) {
        return (needle->obj.v.u32 <= new->v.u32)
            || (needle->obj.v.i32 <= new->v.i32);
    }

    if (needle->obj.flags.f32)
        return (needle->obj.v.f32 <= new->v.f32);

    if (needle->obj.flags.i16) {
        return (needle->obj.v.u16 <= new->v.u16)
            || (needle->obj.v.i16 <= new->v.i16);
    }

    if (needle->obj.flags.i8) {
        return (needle->obj.v.u8 <= new->v.u8)
            || (needle->obj.v.i8 <= new->v.i8);
    }

    return 0;
}

/**
 * Find matches less than or equal to a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to compare against
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_le(pid_t pid, struct match_list *list,
    const struct match_needle *needle)
{
    return __match(pid, list, needle, NULL, __match_le);
}


static int
__match_gt(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)orig;
    (void)unused;

    /* This is more difficult an integer and a floating point
     * have different meaning of greater than. Since integer
     * and floating point flags should be mutually exclusive,
     * ignore the complexity for now. */

    /* Find the largest flag for needle and compare. */
    if (needle->obj.flags.i64) {
        return (needle->obj.v.u64 > new->v.u64)
            || (needle->obj.v.i64 > new->v.i64);
    }

    if (needle->obj.flags.f64)
        return (needle->obj.v.f64 > new->v.f64);

    if (needle->obj.flags.i32) {
        return (needle->obj.v.u32 > new->v.u32)
            || (needle->obj.v.i32 > new->v.i32);
    }

    if (needle->obj.flags.f32)
        return (needle->obj.v.f32 > new->v.f32);

    if (needle->obj.flags.i16) {
        return (needle->obj.v.u16 > new->v.u16)
            || (needle->obj.v.i16 > new->v.i16);
    }

    if (needle->obj.flags.i8) {
        return (needle->obj.v.u8 > new->v.u8)
            || (needle->obj.v.i8 > new->v.i8);
    }

    return 0;
}

/**
 * Find matches greater than a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to compare against
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_gt(pid_t pid, struct match_list *list,
    const struct match_needle *needle)
{
    return __match(pid, list, needle, NULL, __match_gt);
}


static int
__match_ge(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *needle, const struct match_needle *unused)
{
    (void)orig;
    (void)unused;

    /* This is more difficult an integer and a floating point
     * have different meaning of less than. Since integer
     * and floating point flags should be mutually exclusive,
     * ignore the complexity for now. */

    /* Find the largest flag for needle and compare. */
    if (needle->obj.flags.i64) {
        return (needle->obj.v.u64 >= new->v.u64)
            || (needle->obj.v.i64 >= new->v.i64);
    }

    if (needle->obj.flags.f64)
        return (needle->obj.v.f64 >= new->v.f64);

    if (needle->obj.flags.i32) {
        return (needle->obj.v.u32 >= new->v.u32)
            || (needle->obj.v.i32 >= new->v.i32);
    }

    if (needle->obj.flags.f32)
        return (needle->obj.v.f32 >= new->v.f32);

    if (needle->obj.flags.i16) {
        return (needle->obj.v.u16 >= new->v.u16)
            || (needle->obj.v.i16 >= new->v.i16);
    }

    if (needle->obj.flags.i8) {
        return (needle->obj.v.u8 >= new->v.u8)
            || (needle->obj.v.i8 >= new->v.i8);
    }

    return 0;
}

/**
 * Find matches greater than or equal to a value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] needle - value to compare against
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_ge(pid_t pid, struct match_list *list,
    const struct match_needle *needle)
{
    return __match(pid, list, needle, NULL, __match_ge);
}


static int
__match_gt_lt(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *lower, const struct match_needle *upper)
{
    if (!__match_gt(orig, new, lower, NULL))
        return 0;

    return __match_lt(orig, new, upper, NULL);
}

static int
__match_ge_lt(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *lower, const struct match_needle *upper)
{
    if (!__match_ge(orig, new, lower, NULL))
        return 0;

    return __match_lt(orig, new, upper, NULL);
}

static int
__match_gt_le(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *lower, const struct match_needle *upper)
{
    if (!__match_gt(orig, new, lower, NULL))
        return 0;

    return __match_le(orig, new, upper, NULL);
}

static int
__match_ge_le(const struct match_object *orig, const struct match_object *new,
    const struct match_needle *lower, const struct match_needle *upper)
{
    if (!__match_ge(orig, new, lower, NULL))
        return 0;

    return __match_le(orig, new, upper, NULL);
}

/**
 * Find matches within a range in a match list.
 *
 * Possible range flags:
 *   MRBF_GT_LT  : >  lower_bound && <  upper_bound
 *   MRBF_GE_LT  : >= lower_bound && <  upper_bound
 *   MRBF_GT_LE  : >  lower_bound && <= upper_bound
 *   MRBF_GE_LE  : >= lower_bound && <= upper_bound
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to match against
 * @param[in] lower_bound - lower bound value to compare against
 * @param[in] upper_bound - upper bound value to compare against
 * @parma[in] flags - how to process the range
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_range(pid_t pid, struct match_list *list,
    const struct match_needle *lower_bound,
    const struct match_needle *upper_bound,
    enum match_range_bound_flags flags)
{
    match_fn actor;

    switch (flags) {
    case MRBF_GT_LT:
        actor = __match_gt_lt;
        break;

    case MRBF_GE_LT:
        actor = __match_ge_lt;
        break;

    case MRBF_GT_LE:
        actor = __match_gt_le;
        break;

    case MRBF_GE_LE:
        actor = __match_ge_le;
        break;

    default:
        errno = EINVAL;
        return -1;
    }

    return __match(pid, list, lower_bound, upper_bound, actor);
}


static int
__match_changed(const struct match_object *orig,
    const struct match_object *new, const struct match_needle *unused1,
    const struct match_needle *unused2)
{
    (void)unused1;
    (void)unused2;

    /* Find the largest common size flag and compare. */

    if (orig->flags.i64 || orig->flags.f64)
        return (orig->v.u64 != new->v.u64);

    if (orig->flags.i32 || orig->flags.f32)
        return (orig->v.u32 != new->v.u32);

    if (orig->flags.i16)
        return (orig->v.u16 != new->v.u16);

    if (orig->flags.i8)
        return (orig->v.u8 != new->v.u8);

    return 0;
}

/**
 * Find matches that have changed in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_changed(pid_t pid, struct match_list *list)
{
    return __match(pid, list, NULL, NULL, __match_changed);
}


static int
__match_unchanged(const struct match_object *orig,
    const struct match_object *new, const struct match_needle *unused1,
    const struct match_needle *unused2)
{
    (void)unused1;
    (void)unused2;

    /* Find the largest common size flag and compare. */

    if (orig->flags.i64 || orig->flags.f64)
        return (orig->v.u64 == new->v.u64);

    if (orig->flags.i32 || orig->flags.f32)
        return (orig->v.u32 == new->v.u32);

    if (orig->flags.i16)
        return (orig->v.u16 == new->v.u16);

    if (orig->flags.i8)
        return (orig->v.u8 == new->v.u8);

    return 0;
}

/**
 * Find matches that have not changed in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_unchanged(pid_t pid, struct match_list *list)
{
    return __match(pid, list, NULL, NULL, __match_unchanged);
}


static int
__match_decreased(const struct match_object *orig,
    const struct match_object *new, const struct match_needle *unused1,
    const struct match_needle *unused2)
{
    (void)unused1;
    (void)unused2;

    /* This is more difficult an integer and a floating point
     * have different meaning of less than. Since integer
     * and floating point flags should be mutually exclusive,
     * ignore the complexity for now. */

    /* Find the smallest common size flag and compare upwards.
     * Why? Because if any of the possible value times decrease
     * then it is a decreased value.  So, we have to start at
     * the smallest value to make sure we evaluate every case.
     */

    if (orig->flags.i8) {
        if ((new->v.u8 < orig->v.u8) || (new->v.i8 < orig->v.i8))
            return 1;
    }

    if (orig->flags.i16) {
        if ((new->v.u16 < orig->v.u16) || (new->v.i16 < orig->v.i16))
            return 1;
    }

    if (orig->flags.i32) {
        if ((new->v.u32 < orig->v.u32) || (new->v.i32 < orig->v.i32))
            return 1;
    }

    if (orig->flags.f32) {
        if (new->v.f32 < orig->v.f32)
            return 1;
    }

    if (orig->flags.i64) {
        if ((new->v.u64 < orig->v.u64) || (new->v.i64 < orig->v.i64))
            return 1;
    }

    if (orig->flags.f64) {
        if (new->v.f64 < orig->v.f64)
            return 1;
    }

    return 0;
}

/**
 * Find matches that have decreased in value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_decreased(pid_t pid, struct match_list *list)
{
    return __match(pid, list, NULL, NULL, __match_decreased);
}

extern int match_decreased(pid_t pid, struct match_list *list);
extern int match_increased(pid_t pid, struct match_list *list);


static int
__match_increased(const struct match_object *orig,
    const struct match_object *new, const struct match_needle *unused1,
    const struct match_needle *unused2)
{
    (void)unused1;
    (void)unused2;

    /* This is more difficult an integer and a floating point
     * have different meaning of greater than. Since integer
     * and floating point flags should be mutually exclusive,
     * ignore the complexity for now. */

    /* Find the smallest common size flag and compare upwards.
     * Why? Because if any of the possible value times increased
     * then it is an increasing value.  So, we have to start at
     * the smallest value to make sure we evaluate every case.
     */

    if (orig->flags.i8) {
        if ((new->v.u8 > orig->v.u8) || (new->v.i8 > orig->v.i8))
            return 1;
    }

    if (orig->flags.i16) {
        if ((new->v.u16 > orig->v.u16) || (new->v.i16 > orig->v.i16))
            return 1;
    }

    if (orig->flags.i32) {
        if ((new->v.u32 > orig->v.u32) || (new->v.i32 > orig->v.i32))
            return 1;
    }

    if (orig->flags.f32) {
        if (new->v.f32 > orig->v.f32)
            return 1;
    }

    if (orig->flags.i64) {
        if ((new->v.u64 > orig->v.u64) || (new->v.i64 > orig->v.i64))
            return 1;
    }

    if (orig->flags.f64) {
        if (new->v.f64 > orig->v.f64)
            return 1;
    }

    return 0;
}

/**
 * Find matches that have increased in value in the match list.
 *
 * @param[in] pid - process id these matches are for
 * @param list - list to check
 *
 * @return 0 on success
 * @return < 0 on failure with error returned in errno
 */
int
match_increased(pid_t pid, struct match_list *list)
{
    return __match(pid, list, NULL, NULL, __match_increased);
}

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
