/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2012, 2013 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/* This list implementation is based on the Wayland source code */

#ifndef FV_LIST_H
#define FV_LIST_H

#include "fv-util.h"

/**
 * fv_list - linked list
 *
 * The list head is of "fv_list" type, and must be initialized
 * using fv_list_init().  All entries in the list must be of the same
 * type.  The item type must have a "fv_list" member. This
 * member will be initialized by fv_list_insert(). There is no need to
 * call fv_list_init() on the individual item. To query if the list is
 * empty in O(1), use fv_list_empty().
 *
 * Let's call the list reference "fv_list foo_list", the item type as
 * "item_t", and the item member as "fv_list link". The following code
 *
 * The following code will initialize a list:
 *
 *      fv_list_init (foo_list);
 *      fv_list_insert (foo_list, item1);      Pushes item1 at the head
 *      fv_list_insert (foo_list, item2);      Pushes item2 at the head
 *      fv_list_insert (item2, item3);         Pushes item3 after item2
 *
 * The list now looks like [item2, item3, item1]
 *
 * Will iterate the list in ascending order:
 *
 *      item_t *item;
 *      fv_list_for_each(item, foo_list, link) {
 *              Do_something_with_item(item);
 *      }
 */

struct fv_list {
        struct fv_list *prev;
        struct fv_list *next;
};

void
fv_list_init(struct fv_list *list);

void
fv_list_insert(struct fv_list *list, struct fv_list *elm);

void
fv_list_remove(struct fv_list *elm);

int
fv_list_length(struct fv_list *list);

int
fv_list_empty(struct fv_list *list);

void
fv_list_insert_list(struct fv_list *list, struct fv_list *other);

#ifdef __cplusplus
#define FV_LIST_TYPECAST(iterator) (decltype(iterator))
#else
#define FV_LIST_TYPECAST(iterator) (void *)
#endif

/* This assigns to iterator first so that taking a reference to it
 * later in the second step won't be an undefined operation. It
 * assigns the value of list_node rather than 0 so that it is possible
 * have list_node be based on the previous value of iterator. In that
 * respect iterator is just used as a convenient temporary variable.
 * The compiler optimises all of this down to a single subtraction by
 * a constant */
#define fv_list_set_iterator(list_node, iterator, member)       \
        ((iterator) = FV_LIST_TYPECAST(iterator) (list_node),   \
         (iterator) = FV_LIST_TYPECAST(iterator)                \
         ((char *) (iterator) -                                 \
          (((char *) &(iterator)->member) -                     \
           (char *) (iterator))))

#define fv_container_of(ptr, type, member)                      \
        (type *) ((char *) (ptr) - offsetof (type, member))

#define fv_list_for_each(pos, head, member)                             \
        for (fv_list_set_iterator((head)->next, pos, member);           \
             &pos->member != (head);                                    \
             fv_list_set_iterator(pos->member.next, pos, member))

#define fv_list_for_each_safe(pos, tmp, head, member)                   \
        for (fv_list_set_iterator((head)->next, pos, member),           \
                     fv_list_set_iterator((pos)->member.next, tmp, member); \
             &pos->member != (head);                                    \
             pos = tmp,                                                 \
                     fv_list_set_iterator(pos->member.next, tmp, member))

#define fv_list_for_each_reverse(pos, head, member)                     \
        for (fv_list_set_iterator((head)->prev, pos, member);           \
             &pos->member != (head);                                    \
             fv_list_set_iterator(pos->member.prev, pos, member))

#endif /* FV_LIST_H */
