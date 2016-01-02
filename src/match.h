#ifndef H_MATCH
#define H_MATCH

#include <sys/types.h>

#include <stdint.h>

#include "shared/list.h"
#include "region.h"

/* This is really where speed and size become important. There could be
 * hundreds of thousands of matches for any given value.  And even more so
 * for small and common values like '0' and '1' (booleans).
 *
 * In order to get around allocating individual match objects for each and
 * every match.  And to reduce memory overhead by reducing the number of
 * list nodes, we use what we're calling "match chunks".  A match chunk
 * is just a large blob containing an array of match objects.
 *
 * There are five sizes of chunks:
 *  tiny   - 50 objects
 *  small  - 100 objects
 *  medium - 200 objects
 *  large  - 400 objects
 *  huge   - 800 objects
 *
 * TODO: Optimize object numbers to call more closely on page boundaries.
 */

/* 8B on both 32 and 64 */
union match_flags {
    struct {
        unsigned long  i8 : 1;
        unsigned long i16 : 1;
        unsigned long i32 : 1;
        unsigned long i64 : 1;

        unsigned long f32 : 1;
        unsigned long f64 : 1;

        unsigned long ineq_forward : 1;
        unsigned long ineq_reverse : 1;
    };
    unsigned long bytearray_length;
    unsigned long string_length;
};

struct match_object {
    union {
        int8_t   i8;
        uint8_t  u8;

        int16_t  i16;
        uint16_t u16;

        int32_t  i32;
        uint32_t u32;

        int64_t  i64;
        uint64_t u64;

        float    f32; /* Size specified by IEEE 754 */
        double   f64; /* Size specified by IEEE 754 */

        uint8_t bytes[ sizeof(uint64_t) ];
    } v;

    union match_flags flags;
    unsigned long addr;
};

#define MATCH_CHUNK_SIZE_TINY    (50)
#define MATCH_CHUNK_SIZE_SMALL   (100)
#define MATCH_CHUNK_SIZE_MEDIUM  (200)
#define MATCH_CHUNK_SIZE_LARGE   (400)
#define MATCH_CHUNK_SIZE_HUGE    (800)

struct match_chunk_header {
    struct list_head node;
    unsigned long used;
    unsigned long count;
    struct match_object objects[1];
};

struct match_list {
    struct list_head head;
    size_t size;    
};

struct match_needle {
    struct match_object obj;
};

enum match_range_bound_flags {
    MRBF_GT_LT = 0,
    MRBF_GE_LT = 1,
    MRBF_GT_LE = 2,
    MRBF_GE_LE = 3
};

/* Search function options */
#define SEARCH_OPT_UNALIGNED (0x00)
#define SEARCH_OPT_ALIGNED   (0x01)

#define SEARCH_OPT_MASK \
    (SEARCH_OPT_UNALIGNED | SEARCH_OPT_ALIGNED)
/* TODO: add static vs dynamic range options. */

/* Match list functions */

#define match_list_is_empty(match_list) \
    list_is_empty(&((match_list)->head))

static inline void
match_list_init(struct match_list *list)
{
    list_head_init(&(list->head));
    list->size = 0;
}

extern void match_list_clear(struct match_list *list);

/* Match needle functions */

extern int match_needle_init(struct match_needle *needle,
                const char *value);

/* Match functions (already initialized list of match objects) */

extern int match_eq(pid_t pid, struct match_list *list,
                const struct match_needle *needle);
extern int match_ne(pid_t pid, struct match_list *list,
                const struct match_needle *needle);

extern int match_lt(pid_t pid, struct match_list *list,
                const struct match_needle *needle);
extern int match_le(pid_t pid, struct match_list *list,
                const struct match_needle *needle);

extern int match_gt(pid_t pid, struct match_list *list,
                const struct match_needle *needle);
extern int match_ge(pid_t pid, struct match_list *list,
                const struct match_needle *needle);

extern int match_range(pid_t pid, struct match_list *list,
                const struct match_needle *lower_bound,
                const struct match_needle *upper_bound,
                enum match_range_bound_flags flags);

extern int match_changed(pid_t pid, struct match_list *list);
extern int match_unchanged(pid_t pid, struct match_list *list);
extern int match_decreased(pid_t pid, struct match_list *list);
extern int match_increased(pid_t pid, struct match_list *list);

/* Search functions (initalize and create match objects) */

extern int search_eq(pid_t pid, struct match_list *list,
                const struct match_needle *needle,
                const struct region_list *regions,
                int options);

extern int search_ne(pid_t pid, struct match_list *list,
                const struct match_needle *needle,
                const struct region_list *regions,
                int options);

extern int search_lt(pid_t pid, struct match_list *list,
                const struct match_needle *needle,
                const struct region_list *regions,
                int options);

extern int search_le(pid_t pid, struct match_list *list,
                const struct match_needle *needle,
                const struct region_list *regions,
                int options);

extern int search_gt(pid_t pid, struct match_list *list,
                const struct match_needle *needle,
                const struct region_list *regions,
                int options);

extern int search_ge(pid_t pid, struct match_list *list,
                const struct match_needle *needle,
                const struct region_list *regions,
                int options);

extern int search_range(pid_t pid, struct match_list *list,
                const struct match_needle *lower_bound,
                const struct match_needle *upper_bound,
                enum match_range_bound_flags flags,
                const struct region_list *regions,
                int options);

/* TODO: strings and AOB */

#endif /* H_MATCH */
/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
