#ifndef H_LIST
#define H_LIST

#include "util.h"

/* Similar to lists in the linux kernel. */

#define LIST_POISON1 ((struct list_head *)-1)
#define LIST_POISON2 ((struct list_head *)-2)

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

/* Initializer macros */

#define INIT_LIST_HEAD(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = INIT_LIST_HEAD(name)

/* Initializer functions */

static inline void
list_head_init(struct list_head *head)
{
	if (head != NULL) {
		head->next = head;
		head->prev = head;
	}
}

/* Add functions */

static inline void
__list_add(struct list_head *new,
	struct list_head *prev,
	struct list_head *next)
{
	next->prev = new;
	new->next = next;

	new->prev = prev;
	prev->next = new;
}

static inline void
list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

static inline void
list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}


/* Delete functions */

static inline void
__list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

static inline void
__list_del_entry(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
}

static inline void
list_del(struct list_head *entry)
{
	__list_del_entry(entry);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

/* Replace functions */

static inline void
list_replace(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;

	new->prev = old->prev;
	new->prev->next = new;
}

static inline void
list_swap(struct list_head *el1, struct list_head *el2)
{
	struct list_head tmp;

	tmp.next = el1->next;
	tmp.prev = el1->prev;

	list_replace(el2, el1);
	list_replace(&tmp, el2);
}

/* Size/position test functions */

static inline int
list_is_last_entry(const struct list_head *list,
	const struct list_head *head)
{
	return (list->next == head);
}

static inline int
list_is_empty(const struct list_head *head)
{
	return (head->next == head);
}

static inline int
list_is_singular(const struct list_head *head)
{
	return !list_is_empty(head) && (head->next == head->prev);
}

/* Entry macros */

#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

#define list_first_entry(ptr, type, member) \
	list_entry((ptr)->next, type, member)

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

#define list_next_entry(pos, member) \
	list_entry((pos)->member.next, typeof( *(pos) ), member)

#define list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof( *(pos) ), member)

/* Loop macros */

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_safe(pos, n,head) \
	for (pos = (head)->next, n = pos->next; \
		pos != (head); \
		pos = n, n = pos->next)

#endif /* H_LIST */
