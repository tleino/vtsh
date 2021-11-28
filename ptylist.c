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
#include "vbox.h"

#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#include <stdlib.h>
#include <err.h>

struct ptylist {
	struct dpy *dpy;
	struct pty *ptys[100];
	int n_ptys;
	struct widget *widget;
	struct vbox *vbox;
};

static int	ptylist_keypress(XKeyEvent *, void *);

struct ptylist *
ptylist_create(const char *name, struct widget *parent)
{
	struct ptylist *ptylist;
	extern struct dpy *dpy;

	if ((ptylist = calloc(1, sizeof(*ptylist))) == NULL)
		return NULL;

	if ((WIDGET(ptylist) = widget_create(name, parent)) == NULL)
		goto fail;

	if ((ptylist->vbox = vbox_create("vbox", WIDGET(ptylist))) == NULL)
		goto fail;

	ptylist->dpy = dpy;

	widget_set_keypress_callback(WIDGET(ptylist), ptylist_keypress,
	    ptylist);

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
	if (WINDOW(ptylist) != 0)
		XDestroyWindow(DPY(ptylist->dpy), WINDOW(ptylist));
	free(ptylist);
}

static int
ptylist_keypress(XKeyEvent *xkey, void *udata)
{
	struct ptylist *ptylist = udata;
	KeySym sym;

	if (!(xkey->state & Mod1Mask))
		return 0;

	sym = XkbKeycodeToKeysym(DPY(ptylist->dpy), xkey->keycode, 0,
	    (xkey->state & ShiftMask) ? 1 : 0);

	switch (sym) {
	case XK_Insert:
		if ((ptylist->ptys[ptylist->n_ptys] = pty_create(ptylist->dpy,
		    WIDGET(ptylist->vbox))) == NULL)
			warn("creating pty");
		else
			ptylist->n_ptys++;
		return 1;
	}

	return 0;
}
