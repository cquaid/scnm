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

#endif /* H_UTIL */
