/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

  linux/include/linux/event_rbtree.h

  To use event_rbtrees you'll have to implement your own insert and search cores.
  This will avoid us to use callbacks and to drop drammatically performances.
  I know it's not the cleaner way,  but in C (not in C++) to get
  performances and genericity...

  Some example of insert and search follows here. The search is a plain
  normal search over an ordered tree. The insert instead must be implemented
  in two steps: First, the code must insert the element in order as a red leaf
  in the tree, and then the support library function event_rb_insert_color() must
  be called. Such function will do the not trivial work to rebalance the
  event_rbtree, if necessary.

-----------------------------------------------------------------------
static inline struct page * event_rb_search_page_cache(struct inode * inode,
						 unsigned long offset)
{
	struct event_rb_node * n = inode->i_event_rb_page_cache.event_rb_node;
	struct page * page;

	while (n)
	{
		page = event_rb_entry(n, struct page, event_rb_page_cache);

		if (offset < page->offset)
			n = n->event_rb_left;
		else if (offset > page->offset)
			n = n->event_rb_right;
		else
			return page;
	}
	return NULL;
}

static inline struct page * __event_rb_insert_page_cache(struct inode * inode,
						   unsigned long offset,
						   struct event_rb_node * node)
{
	struct event_rb_node ** p = &inode->i_event_rb_page_cache.event_rb_node;
	struct event_rb_node * parent = NULL;
	struct page * page;

	while (*p)
	{
		parent = *p;
		page = event_rb_entry(parent, struct page, event_rb_page_cache);

		if (offset < page->offset)
			p = &(*p)->event_rb_left;
		else if (offset > page->offset)
			p = &(*p)->event_rb_right;
		else
			return page;
	}

	event_rb_link_node(node, parent, p);

	return NULL;
}

static inline struct page * event_rb_insert_page_cache(struct inode * inode,
						 unsigned long offset,
						 struct event_rb_node * node)
{
	struct page * ret;
	if ((ret = __event_rb_insert_page_cache(inode, offset, node)))
		goto out;
	event_rb_insert_color(node, &inode->i_event_rb_page_cache);
 out:
	return ret;
}
-----------------------------------------------------------------------
*/

#ifndef	_LINUX_EVENT_RBTREE_H
#define	_LINUX_EVENT_RBTREE_H

#include <stddef.h>
#include <linux/kernel.h>
#ifdef CONFIG_EVENT_WIN32_BUILD
#define inline __inline
#endif

struct event_rb_node
{
	unsigned long  event_rb_parent_color;
#define	EVENT_RB_RED		0
#define	EVENT_RB_BLACK	1
	struct event_rb_node *event_rb_right;
	struct event_rb_node *event_rb_left;
};
    /* The alignment might seem pointless, but allegedly CRIS needs it */

struct event_rb_root
{
	struct event_rb_node *rb_leftmost;
	struct event_rb_node *event_rb_node;
};


#define event_rb_parent(r)   ((struct event_rb_node *)((r)->event_rb_parent_color & ~3))
#define event_rb_color(r)   ((r)->event_rb_parent_color & 1)
#define event_rb_is_red(r)   (!event_rb_color(r))
#define event_rb_is_black(r) event_rb_color(r)
#define event_rb_set_red(r)  do { (r)->event_rb_parent_color &= ~1; } while (0)
#define event_rb_set_black(r)  do { (r)->event_rb_parent_color |= 1; } while (0)

static inline void event_rb_set_parent(struct event_rb_node *event_rb, struct event_rb_node *p)
{
	event_rb->event_rb_parent_color = (event_rb->event_rb_parent_color & 3) | (unsigned long)p;
}
static inline void event_rb_set_color(struct event_rb_node *event_rb, int color)
{
	event_rb->event_rb_parent_color = (event_rb->event_rb_parent_color & ~1) | color;
}

#define EVENT_RB_ROOT	(struct event_rb_root) { NULL, }
#define	event_rb_entry(ptr, type, member) \
	(type *)((char *)ptr - (char *)&((type *)0)->member)

#define EVENT_RB_EMPTY_ROOT(root)	((root)->event_rb_node == NULL)
#define EVENT_RB_EMPTY_NODE(node)	(event_rb_parent(node) == node)
#define EVENT_RB_CLEAR_NODE(node)	(event_rb_set_parent(node, node))

static inline void event_rb_init_node(struct event_rb_node *event_rb)
{
	event_rb->event_rb_parent_color = 0;
	event_rb->event_rb_right = NULL;
	event_rb->event_rb_left = NULL;
	EVENT_RB_CLEAR_NODE(event_rb);
}

extern void event_rb_insert_color(struct event_rb_node *, struct event_rb_root *);
extern void event_rb_erase(struct event_rb_node *, struct event_rb_root *);

typedef void (*event_rb_augment_f)(struct event_rb_node *node, void *data);

extern void event_rb_augment_insert(struct event_rb_node *node,
			      event_rb_augment_f func, void *data);
extern struct event_rb_node *event_rb_augment_erase_begin(struct event_rb_node *node);
extern void event_rb_augment_erase_end(struct event_rb_node *node,
				 event_rb_augment_f func, void *data);

/* Find logical next and previous nodes in a tree */
extern struct event_rb_node *event_rb_next(const struct event_rb_node *);
extern struct event_rb_node *event_rb_prev(const struct event_rb_node *);
extern struct event_rb_node *event_rb_first(const struct event_rb_root *);
extern struct event_rb_node *event_rb_last(const struct event_rb_root *);

/* Fast replacement of a single node without remove/rebalance/add/rebalance */
extern void event_rb_replace_node(struct event_rb_node *victim, struct event_rb_node *new, 
			    struct event_rb_root *root);

static inline void event_rb_link_node(struct event_rb_node * node, struct event_rb_node * parent,
				struct event_rb_node ** event_rb_link)
{
	node->event_rb_parent_color = (unsigned long )parent;
	node->event_rb_left = node->event_rb_right = NULL;

	*event_rb_link = node;
}

void event_rb_insert(struct event_rb_root *root, struct event_rb_node *node,
							int (*cmp)(struct event_rb_node *, struct event_rb_node *));

#endif	/* _LINUX_EVENT_RBTREE_H */
