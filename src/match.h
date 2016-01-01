#ifndef H_MATCH
#define H_MATCH

#include <stdbool.h>
#include <stdint.h>

#include "shared/list.h"

/* This is really where speed and size become important. There could be
 * hundreds of thousands of matches for any given value.  And even more so
 * for small and common values like '0' and '1' (booleans).
 *
 * In order to get around allocating individual match objects for each and
 * every match.  And to reduce memory overhead by reducing the number of
 * list nodes, we use what we're calling "match chunks".  A match chunk
 * is just a large blob containing an array of match objects.
 *
 * There are four sizes of chunks:
 *  match_chunk_large  - 4096
 *  match_chunk_medium - 2048
 *  match_chunk_small  - 1024
 *  match_chunk_tiny   -  512
 *
 *
 * A match object is 128 bits (16 bytes) in length.
 *
 * The match_chunk_header is either 16 or 32 bytes depending on architecture.
 *
 * So, the number of elements in a given chunk is:
 *   (sizeof(match_chunk) - sizeof(match_header)) / sizeof(match_object)
 *
 * The following sizes are accurate:
 *  type               | chunk size | # objects (32)  | # objects (64)
 * ---------------------------------------------------------------------
 *  match_chunk_large  | 4096       | 255             | 254
 *  match_chunk_medium | 2048       | 127             | 126
 *  match_chunk_small  | 1024       | 63              | 62
 *  match_chunk_tiny   | 512        | 31              | 30
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
};

#define MATCH_CHUNK_TYPE_LARGE   (0x00)
#define MATCH_CHUNK_TYPE_MEDIUM  (0x01)
#define MATCH_CHUNK_TYPE_SMALL   (0x02)
#define MATCH_CHUNK_TYPE_TINY    (0x03)

struct match_chunk_header {
    struct list_head node;
    unsigned long type; /* Unsigned long used for alignment. */
    unsigned long used;
};


#define MATCH_CHUNK_NELEMENTS(sz) \
    ((MATCH_CHUNK_SIZE_##sz - sizeof(struct match_chunk_header)) \
        / sizeof(struct match_object))

#define DEFINE_MATCH_CHUNK(sz) \
    struct match_chunk_##sz { \
        struct match_chunk_header head; \
        struct match_object objects[ MATCH_CHUNK_NELEMENTS(sz) ]; \
    }

#define MATCH_CHUNK_SIZE_large  (4096)
#define MATCH_CHUNK_SIZE_medium (2048)
#define MATCH_CHUNK_SIZE_small  (1024)
#define MATCH_CHUNK_SIZE_tiny   (512)

DEFINE_MATCH_CHUNK( large  );
DEFINE_MATCH_CHUNK( medium );
DEFINE_MATCH_CHUNK( small  );
DEFINE_MATCH_CHUNK( tiny   );



extern bool
match_flags_set_integer(const char *value, union match_flags *flags);

extern bool
match_flags_set_floating(const char *value, union match_flags *flags);

/* TODO: strings and AOB */

#endif /* H_MATCH */
