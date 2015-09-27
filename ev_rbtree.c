/*
  Red Black Trees
  (C) 1999  Andrea Arcangeli <andrea@suse.de>
  (C) 2002  David Woodhouse <dwmw2@infradead.org>

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

  linux/lib/event_rbtree.c
*/
#define EXPORT_SYMBOL(s)
#include <ev_rbtree.h>

static void __event_rb_rotate_left(struct event_rb_node *node, struct event_rb_root *root)
{
	struct event_rb_node *right = node->event_rb_right;
	struct event_rb_node *parent = event_rb_parent(node);

	if ((node->event_rb_right = right->event_rb_left))
		event_rb_set_parent(right->event_rb_left, node);
	right->event_rb_left = node;

	event_rb_set_parent(right, parent);

	if (parent)
	{
		if (node == parent->event_rb_left)
			parent->event_rb_left = right;
		else
			parent->event_rb_right = right;
	}
	else
		root->event_rb_node = right;
	event_rb_set_parent(node, right);
}

static void __event_rb_rotate_right(struct event_rb_node *node, struct event_rb_root *root)
{
	struct event_rb_node *left = node->event_rb_left;
	struct event_rb_node *parent = event_rb_parent(node);

	if ((node->event_rb_left = left->event_rb_right))
		event_rb_set_parent(left->event_rb_right, node);
	left->event_rb_right = node;

	event_rb_set_parent(left, parent);

	if (parent)
	{
		if (node == parent->event_rb_right)
			parent->event_rb_right = left;
		else
			parent->event_rb_left = left;
	}
	else
		root->event_rb_node = left;
	event_rb_set_parent(node, left);
}

void event_rb_insert_color(struct event_rb_node *node, struct event_rb_root *root)
{
	struct event_rb_node *parent, *gparent;

	while ((parent = event_rb_parent(node)) && event_rb_is_red(parent))
	{
		gparent = event_rb_parent(parent);

		if (parent == gparent->event_rb_left)
		{
			{
				register struct event_rb_node *uncle = gparent->event_rb_right;
				if (uncle && event_rb_is_red(uncle))
				{
					event_rb_set_black(uncle);
					event_rb_set_black(parent);
					event_rb_set_red(gparent);
					node = gparent;
					continue;
				}
			}

			if (parent->event_rb_right == node)
			{
				register struct event_rb_node *tmp;
				__event_rb_rotate_left(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			event_rb_set_black(parent);
			event_rb_set_red(gparent);
			__event_rb_rotate_right(gparent, root);
		} else {
			{
				register struct event_rb_node *uncle = gparent->event_rb_left;
				if (uncle && event_rb_is_red(uncle))
				{
					event_rb_set_black(uncle);
					event_rb_set_black(parent);
					event_rb_set_red(gparent);
					node = gparent;
					continue;
				}
			}

			if (parent->event_rb_left == node)
			{
				register struct event_rb_node *tmp;
				__event_rb_rotate_right(parent, root);
				tmp = parent;
				parent = node;
				node = tmp;
			}

			event_rb_set_black(parent);
			event_rb_set_red(gparent);
			__event_rb_rotate_left(gparent, root);
		}
	}

	event_rb_set_black(root->event_rb_node);
}
EXPORT_SYMBOL(event_rb_insert_color);

static void __event_rb_erase_color(struct event_rb_node *node, struct event_rb_node *parent,
			     struct event_rb_root *root)
{
	struct event_rb_node *other;

	while ((!node || event_rb_is_black(node)) && node != root->event_rb_node)
	{
		if (parent->event_rb_left == node)
		{
			other = parent->event_rb_right;
			if (event_rb_is_red(other))
			{
				event_rb_set_black(other);
				event_rb_set_red(parent);
				__event_rb_rotate_left(parent, root);
				other = parent->event_rb_right;
			}
			if ((!other->event_rb_left || event_rb_is_black(other->event_rb_left)) &&
			    (!other->event_rb_right || event_rb_is_black(other->event_rb_right)))
			{
				event_rb_set_red(other);
				node = parent;
				parent = event_rb_parent(node);
			}
			else
			{
				if (!other->event_rb_right || event_rb_is_black(other->event_rb_right))
				{
					event_rb_set_black(other->event_rb_left);
					event_rb_set_red(other);
					__event_rb_rotate_right(other, root);
					other = parent->event_rb_right;
				}
				event_rb_set_color(other, event_rb_color(parent));
				event_rb_set_black(parent);
				event_rb_set_black(other->event_rb_right);
				__event_rb_rotate_left(parent, root);
				node = root->event_rb_node;
				break;
			}
		}
		else
		{
			other = parent->event_rb_left;
			if (event_rb_is_red(other))
			{
				event_rb_set_black(other);
				event_rb_set_red(parent);
				__event_rb_rotate_right(parent, root);
				other = parent->event_rb_left;
			}
			if ((!other->event_rb_left || event_rb_is_black(other->event_rb_left)) &&
			    (!other->event_rb_right || event_rb_is_black(other->event_rb_right)))
			{
				event_rb_set_red(other);
				node = parent;
				parent = event_rb_parent(node);
			}
			else
			{
				if (!other->event_rb_left || event_rb_is_black(other->event_rb_left))
				{
					event_rb_set_black(other->event_rb_right);
					event_rb_set_red(other);
					__event_rb_rotate_left(other, root);
					other = parent->event_rb_left;
				}
				event_rb_set_color(other, event_rb_color(parent));
				event_rb_set_black(parent);
				event_rb_set_black(other->event_rb_left);
				__event_rb_rotate_right(parent, root);
				node = root->event_rb_node;
				break;
			}
		}
	}
	if (node)
		event_rb_set_black(node);
}

void event_rb_erase(struct event_rb_node *node, struct event_rb_root *root)
{
	struct event_rb_node *child, *parent;
	int color;

	if (!node->event_rb_left)
		child = node->event_rb_right;
	else if (!node->event_rb_right)
		child = node->event_rb_left;
	else
	{
		struct event_rb_node *old = node, *left;

		node = node->event_rb_right;
		while ((left = node->event_rb_left) != NULL)
			node = left;

		if (event_rb_parent(old)) {
			if (event_rb_parent(old)->event_rb_left == old)
				event_rb_parent(old)->event_rb_left = node;
			else
				event_rb_parent(old)->event_rb_right = node;
		} else
			root->event_rb_node = node;

		child = node->event_rb_right;
		parent = event_rb_parent(node);
		color = event_rb_color(node);

		if (parent == old) {
			parent = node;
		} else {
			if (child)
				event_rb_set_parent(child, parent);
			parent->event_rb_left = child;

			node->event_rb_right = old->event_rb_right;
			event_rb_set_parent(old->event_rb_right, node);
		}

		node->event_rb_parent_color = old->event_rb_parent_color;
		node->event_rb_left = old->event_rb_left;
		event_rb_set_parent(old->event_rb_left, node);

		goto color;
	}

	parent = event_rb_parent(node);
	color = event_rb_color(node);

	if (child)
		event_rb_set_parent(child, parent);
	if (parent)
	{
		if (parent->event_rb_left == node)
			parent->event_rb_left = child;
		else
			parent->event_rb_right = child;
	}
	else
		root->event_rb_node = child;

 color:
	if (color == EVENT_RB_BLACK)
		__event_rb_erase_color(child, parent, root);
}
EXPORT_SYMBOL(event_rb_erase);

static void event_rb_augment_path(struct event_rb_node *node, event_rb_augment_f func, void *data)
{
	struct event_rb_node *parent;

up:
	func(node, data);
	parent = event_rb_parent(node);
	if (!parent)
		return;

	if (node == parent->event_rb_left && parent->event_rb_right)
		func(parent->event_rb_right, data);
	else if (parent->event_rb_left)
		func(parent->event_rb_left, data);

	node = parent;
	goto up;
}

/*
 * after inserting @node into the tree, update the tree to account for
 * both the new entry and any damage done by rebalance
 */
void event_rb_augment_insert(struct event_rb_node *node, event_rb_augment_f func, void *data)
{
	if (node->event_rb_left)
		node = node->event_rb_left;
	else if (node->event_rb_right)
		node = node->event_rb_right;

	event_rb_augment_path(node, func, data);
}
EXPORT_SYMBOL(event_rb_augment_insert);

/*
 * before removing the node, find the deepest node on the rebalance path
 * that will still be there after @node gets removed
 */
struct event_rb_node *event_rb_augment_erase_begin(struct event_rb_node *node)
{
	struct event_rb_node *deepest;

	if (!node->event_rb_right && !node->event_rb_left)
		deepest = event_rb_parent(node);
	else if (!node->event_rb_right)
		deepest = node->event_rb_left;
	else if (!node->event_rb_left)
		deepest = node->event_rb_right;
	else {
		deepest = event_rb_next(node);
		if (deepest->event_rb_right)
			deepest = deepest->event_rb_right;
		else if (event_rb_parent(deepest) != node)
			deepest = event_rb_parent(deepest);
	}

	return deepest;
}
EXPORT_SYMBOL(event_rb_augment_erase_begin);

/*
 * after removal, update the tree to account for the removed entry
 * and any rebalance damage.
 */
void event_rb_augment_erase_end(struct event_rb_node *node, event_rb_augment_f func, void *data)
{
	if (node)
		event_rb_augment_path(node, func, data);
}
EXPORT_SYMBOL(event_rb_augment_erase_end);

/*
 * This function returns the first node (in sort order) of the tree.
 */
struct event_rb_node *event_rb_first(const struct event_rb_root *root)
{
	struct event_rb_node	*n;

	n = root->event_rb_node;
	if (!n)
		return NULL;
	while (n->event_rb_left)
		n = n->event_rb_left;
	return n;
}
EXPORT_SYMBOL(event_rb_first);

struct event_rb_node *event_rb_last(const struct event_rb_root *root)
{
	struct event_rb_node	*n;

	n = root->event_rb_node;
	if (!n)
		return NULL;
	while (n->event_rb_right)
		n = n->event_rb_right;
	return n;
}
EXPORT_SYMBOL(event_rb_last);

struct event_rb_node *event_rb_next(const struct event_rb_node *node)
{
	struct event_rb_node *parent;

	if (event_rb_parent(node) == node)
		return NULL;

	/* If we have a right-hand child, go down and then left as far
	   as we can. */
	if (node->event_rb_right) {
		node = node->event_rb_right;
		while (node->event_rb_left)
			node=node->event_rb_left;
		return (struct event_rb_node *)node;
	}

	/* No right-hand children.  Everything down and left is
	   smaller than us, so any 'next' node must be in the general
	   direction of our parent. Go up the tree; any time the
	   ancestor is a right-hand child of its parent, keep going
	   up. First time it's a left-hand child of its parent, said
	   parent is our 'next' node. */
	while ((parent = event_rb_parent(node)) && node == parent->event_rb_right)
		node = parent;

	return parent;
}
EXPORT_SYMBOL(event_rb_next);

struct event_rb_node *event_rb_prev(const struct event_rb_node *node)
{
	struct event_rb_node *parent;

	if (event_rb_parent(node) == node)
		return NULL;

	/* If we have a left-hand child, go down and then right as far
	   as we can. */
	if (node->event_rb_left) {
		node = node->event_rb_left;
		while (node->event_rb_right)
			node=node->event_rb_right;
		return (struct event_rb_node *)node;
	}

	/* No left-hand children. Go up till we find an ancestor which
	   is a right-hand child of its parent */
	while ((parent = event_rb_parent(node)) && node == parent->event_rb_left)
		node = parent;

	return parent;
}
EXPORT_SYMBOL(event_rb_prev);

void event_rb_replace_node(struct event_rb_node *victim, struct event_rb_node *new,
		     struct event_rb_root *root)
{
	struct event_rb_node *parent = event_rb_parent(victim);

	/* Set the surrounding nodes to point to the replacement */
	if (parent) {
		if (victim == parent->event_rb_left)
			parent->event_rb_left = new;
		else
			parent->event_rb_right = new;
	} else {
		root->event_rb_node = new;
	}
	if (victim->event_rb_left)
		event_rb_set_parent(victim->event_rb_left, new);
	if (victim->event_rb_right)
		event_rb_set_parent(victim->event_rb_right, new);

	/* Copy the pointers/colour from the victim to the replacement */
	*new = *victim;
}
EXPORT_SYMBOL(event_rb_replace_node);

void event_rb_insert(struct event_rb_root *root, struct event_rb_node *node,
							int (*cmp)(struct event_rb_node *, struct event_rb_node *))
{
	int is_leftmost = 1;
	struct event_rb_node **new = &(root->event_rb_node), *parent = NULL;

	/* Figure out where to put new node */
	while (*new) {
		int result = cmp(node, *new);

		parent = *new;
		if (result < 0)
			new = &((*new)->event_rb_left);
		else if (result > 0) {
			new = &((*new)->event_rb_right);
			is_leftmost = 0;
		} else
			return;
	}

	if (is_leftmost)
		root->rb_leftmost = node;

	/* Add new node and rebalance tree. */
	event_rb_link_node(node, parent, new);
	event_rb_insert_color(node, root);
}

