#ifndef H_UTIL
#define H_UTIL

#define ARRAY_SIZ(x) (sizeof(x) / sizeof(*x))

#ifndef offsetof
#define offsetof(type, member) \
    ((size_t) & ( ((type *)0)->member ))
#endif

#define container_of(ptr, type, member) \
    ({ \
        const typeof( ((type *)0)->member ) *__mptr = (ptr); \
        (type *)( (char *)__mptr - offsetof(type, member) ); \
    })

#define SWAP(a, b) \
    do { \
        typeof( a ) SWAP__tmp = (b); \
        (b) = (a); \
        (a) = (SWAP__tmp); \
    } while (0)

#endif /* H_UTIL */

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
