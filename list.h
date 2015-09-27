#ifndef _EVENT_H
#define _EVENT_H


#define EXPORT_SYMBOL(s) 

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif
#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)( \
		(char *)(ptr) - \
		(unsigned long)(&((type *)0)->member)))
#endif


struct event_list {
	struct event_list *prev, *next;
};

/**
 * INIT_LIST_HEAD - Initialize a list head.
 * @list: The list head to be reset.
 */
static inline void INIT_LIST_HEAD(struct event_list *list)
{
	list->next = list;
	list->prev = list;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __event_list_add(struct event_list *newp,
                              struct event_list *prev,
                              struct event_list *next)
{
	next->prev = newp;
	newp->next = next;
	newp->prev = prev;
	prev->next = newp;
}

/**
 * event_list_add - add a newp entry
 * @newp: newp entry to be added
 * @head: list head to add it after
 *
 * Insert a newp entry after the specified head.
 */
static inline void event_list_add(struct event_list *newp, struct event_list *head)
{
	__event_list_add(newp, head, head->next);
}

/**
 * event_list_add_tail - add a newp entry
 * @newp: newp entry to be added
 * @head: list head to add it before
 *
 * Insert a newp entry before the specified head.
 */
static inline void event_list_add_tail(struct event_list *newp, struct event_list *head)
{
	__event_list_add(newp, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __event_list_del(struct event_list * prev, struct event_list * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * event_list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: evlist_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void event_list_del(struct event_list *entry)
{
	__event_list_del(entry->prev, entry->next);
	entry->next = entry;
	entry->prev = entry;
}

/**
 * event_list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int event_list_empty(struct event_list *head)
{
	return head->next == head;
}

/**
 * event_list_first_entry - get the first element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the evlist_struct within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define event_list_first_entry(ptr, type, member) \
	container_of((ptr)->next, type, member)

/**
 * event_list_for_each_safe - iterate over a list safe against removal of list entry
 * @pos:	the &struct evlist_head to use as a loop cursor.
 * @n:		another &struct evlist_head to use as temporary storage
 * @head:	the head for your list.
 */
#define event_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
	pos = n, n = pos->next)

#endif

