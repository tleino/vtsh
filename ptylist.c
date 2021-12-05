/*
 * vtsh - A mashup of virtual terminal and shell
 * Copyright (c) 2021, Tommi Leino <namhas@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ptylist.h"
#include "dpy.h"
#include "pty.h"
#include "widget.h"
#include "layout.h"
#include "editor.h"

#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <assert.h>
#include <stdio.h>

struct ptylist {
	struct dpy *dpy;
	struct pty *ptys[100];
	int n_ptys;
	int i;
	struct widget *widget;
	struct layout *vbox;
};

static int	ptylist_keypress(XKeyEvent *, void *);
static void	ptylist_add_pty(struct ptylist *);
static int	ptylist_find_pty(struct ptylist *, struct widget *);

struct ptylist *
ptylist_create(const char *name, struct widget *parent)
{
	struct ptylist *ptylist;
	extern struct dpy *dpy;

	if ((ptylist = calloc(1, sizeof(*ptylist))) == NULL)
		return NULL;

	if ((WIDGET(ptylist) = widget_create(name, parent)) == NULL)
		goto fail;

	if ((ptylist->vbox = layout_create_vbox("vbox", WIDGET(ptylist)))
	    == NULL)
		goto fail;

	ptylist->dpy = dpy;

	widget_set_keypress_callback(WIDGET(ptylist), ptylist_keypress,
	    ptylist);

	ptylist_add_pty(ptylist);

	widget_show(WIDGET(ptylist->vbox));
	widget_show(WIDGET(ptylist));

	return ptylist;
fail:
	ptylist_free(ptylist);
	return NULL;	
}

void
ptylist_free(struct ptylist *ptylist)
{
	widget_free(WIDGET(ptylist));
	free(ptylist);
}

static void
ptylist_add_pty(struct ptylist *ptylist)
{
	int i;
	struct widget *root, *ptywidget = NULL;
	char name[256];

	root = widget_find_root(WIDGET(ptylist));
	ptywidget = root->focus;
	i = ptylist_find_pty(ptylist, ptywidget);
	if (i == -1) {
		i = ptylist->n_ptys;
		ptywidget = NULL;
	} else
		ptywidget = WIDGET(ptylist->ptys[i]);

	memmove(&ptylist->ptys[i+1], &ptylist->ptys[i],
	    (ptylist->n_ptys-i) * sizeof(struct pty *));

	snprintf(name, sizeof(name), "pty%d", ++ptylist->i);
	if ((ptylist->ptys[i] = pty_create(ptylist->dpy, name,
	    WIDGET(ptylist->vbox))) == NULL)
		warn("creating pty");
	else
		ptylist->n_ptys++;

	if (ptywidget != NULL)
		widget_move_after(WIDGET(ptylist->ptys[i]), ptywidget);
	widget_focus(WIDGET(ptylist->ptys[i]));
}

static int
ptylist_find_pty(struct ptylist *ptylist, struct widget *widget)
{
	int i;
	struct widget *np;

	for (i = 0; i < ptylist->n_ptys; i++) {
		np = widget;
		while (np && np->parent != NULL) {
			np = np->parent;
			if (WIDGET(ptylist->ptys[i]) == np)
				return i;
		}
	}
	return -1;
}

static int
ptylist_keypress(XKeyEvent *xkey, void *udata)
{
	struct ptylist *ptylist = udata;
	KeySym sym;
	struct widget *root, *ptywidget;
	int pty;

	sym = XkbKeycodeToKeysym(DPY(ptylist->dpy), xkey->keycode, 0,
	    (xkey->state & ShiftMask) ? 1 : 0);

	if (!(xkey->state & Mod1Mask) && sym != XK_Escape)
		return 0;

	switch (sym) {
	case XK_Insert:
		ptylist_add_pty(ptylist);
		return 1;
	case XK_Escape:
		root = widget_find_root(WIDGET(ptylist));
		if (root->level == 1)
			root->level ^= 1;
		widget_focus_prev(root->focus, root->level);
		return 1;
	case XK_BackSpace:
		root = widget_find_root(WIDGET(ptylist));
		ptywidget = root->focus;

		/*
		 * Ensure focus can land on a new pty, refuse otherwise.
		 */
		widget_focus_prev(ptywidget, root->level);
		if (root->focus == ptywidget) {
			widget_focus_next(ptywidget, root->level);
			if (root->focus == ptywidget) {
				widget_focus(ptywidget);
				return 1;
			}
		}

		/*
		 * Remove it.
		 */
		pty = ptylist_find_pty(ptylist, ptywidget);
		if (pty >= 0) {
			pty_free(ptylist->ptys[pty]);
			if (pty+1 < ptylist->n_ptys)
				memmove(&ptylist->ptys[pty],
				    &ptylist->ptys[pty+1],
				    (ptylist->n_ptys-pty-1) *
				    sizeof(struct pty *));
			ptylist->n_ptys--;
		} else
			assert(0);
		return 1;
	case XK_Return:
		root = widget_find_root(WIDGET(ptylist));
		if (root->level == 0)
			root->level ^= 1;
		widget_focus_next(root->focus, root->level);
		return 1;
	}

	return 0;
}
