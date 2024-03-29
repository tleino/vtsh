/*
 * vtsh - A mashup of virtual terminal and shell
 * Copyright (c) 2021-2022, Tommi Leino <namhas@gmail.com>
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
#include "xevent.h"
#include "font.h"
#include "button.h"
#include "util.h"
#include "event.h"

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
	struct ptylist *first;
	struct ptylist *next;

	struct widget	*context_menu;
	struct pty	*context_pty;
	char		*context_s;
};

static int		 ptylist_keypress(XKeyEvent *, void *);
static void		 ptylist_focus_change(int, void *);
static struct pty	*ptylist_add_pty(struct ptylist *, struct pty *);
static int		 ptylist_find_pty(struct ptylist *, struct widget *);
static void		 ptylist_destroy(void *);
static void		 ptylist_create_new_window(void);
static void		 ptylist_close_pty(struct ptylist *, struct pty *);

static void		 ptylist_context_open(struct ptylist *,
			    struct pty *, const char *, int, int);
static void		 ptylist_context_close(struct ptylist *);

static void		 ptylist_ptyaction(struct pty *, PtyAction,
			    const char *, int, int, void *);

static int		 n_ptylist;
static int		 ptylist_i = 1;
struct ptylist		*ptylist_root;

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
	widget_set_focus_change_callback(WIDGET(ptylist),
	    ptylist_focus_change, ptylist);

	ptylist_add_pty(ptylist, NULL);

	widget_show(WIDGET(ptylist->vbox));
	widget_show(WIDGET(ptylist));

	add_destroy_handler(WIDGET(ptylist)->window, ptylist_destroy, ptylist);

	if (n_ptylist == 0)
		ptylist_root = ptylist;
	n_ptylist++;

	return ptylist;
fail:
	ptylist_free(ptylist);
	return NULL;	
}

static void
ptylist_destroy(void *udata)
{
	struct ptylist *ptylist = udata;
	extern struct dpy *dpy;
	
	XSync(DPY(dpy), False);
	font_close();

	ptylist_free(ptylist);
}

void
ptylist_free_all()
{
	struct ptylist *np, *next;

	if (ptylist_root) {
		for (np = ptylist_root->first; np != NULL; np = next) {
			next = np->next;
			free(np);
			n_ptylist--;
		}
		free(ptylist_root);
		ptylist_root = NULL;
		n_ptylist--;
	}
}

void
ptylist_free(struct ptylist *ptylist)
{
	int i;
	struct ptylist *np;
	extern int running;

	if (ptylist == ptylist_root) {
		ptylist_root = ptylist_root->first;
		if (ptylist_root != NULL && ptylist_root->first != NULL)
			ptylist_root->first = ptylist_root->first->next;
	} else if (ptylist_root != NULL && ptylist_root->first == ptylist) {
		ptylist_root->first = ptylist_root->first->next;
	} else if (ptylist_root != NULL) {
		np = ptylist_root->first;
		while (np) {
			if (np->next == ptylist) {
				np->next = np->next->next;
				break;
			}
			np = np->next;
		}
	}

	for (i = 0; i < ptylist->n_ptys; i++)
		pty_free(ptylist->ptys[i]);

	if (ptylist->vbox != NULL)
		layout_free(ptylist->vbox);

	if (ptylist->context_menu != NULL)
		widget_free(ptylist->context_menu);

	widget_free(WIDGET(ptylist));
	free(ptylist);

	n_ptylist--;
	if (n_ptylist == 0)
		running = 0;
}

static void
ptylist_context_close(struct ptylist *ptylist)
{
	if (ptylist->context_menu == NULL)
		return;

	widget_hide(ptylist->context_menu);
	ptylist->context_pty = NULL;
	if (ptylist->context_s != NULL) {
		free(ptylist->context_s);
		ptylist->context_s = NULL;
	}
}

static void
ptylist_open_button(struct button *button, void *udata)
{
	struct ptylist *ptylist = udata;
	struct pty *pty;
	char *s;

	if (ptylist->context_menu == NULL)
		return;

	s = malloc(strlen(ptylist->context_s) + 2);
	if (s != NULL) {
		snprintf(s, strlen(ptylist->context_s) + 2,
		    ":%s", ptylist->context_s);
		pty = ptylist_add_pty(ptylist, NULL);
		pty_run_command(pty, s);
		free(s);
	}

	ptylist_context_close(ptylist);
}

static void
ptylist_exec_button(struct button *button, void *udata)
{
	struct ptylist *ptylist = udata;
	struct pty *pty;

	if (ptylist->context_menu == NULL)
		return;

	pty = ptylist_add_pty(ptylist, NULL);
	pty_run_command(pty, ptylist->context_s);

	ptylist_context_close(ptylist);
}

static void
ptylist_context_open(struct ptylist *ptylist, struct pty *pty,
    const char *s, int x, int y)
{
	struct button *open_button, *exec_button;
	struct layout *vbox;
	extern struct dpy *dpy;

	if (ptylist->context_menu != NULL) {
		XMoveWindow(DPY(dpy), ptylist->context_menu->window, x, y);
		XRaiseWindow(DPY(dpy), ptylist->context_menu->window);
		widget_show(ptylist->context_menu);
	} else {
		ptylist->context_menu = widget_create_transient("context_menu",
		    widget_find_root(WIDGET(ptylist)));
		if (ptylist->context_menu == NULL)
			return;
	}

	ptylist->context_pty = pty;
	ptylist->context_s = strdup(s);

	XMoveWindow(DPY(dpy), ptylist->context_menu->window, x, y);
	vbox = layout_create_vbox("context_vbox", ptylist->context_menu);
	open_button = button_create("open", ptylist_open_button, ptylist,
	    "open", WIDGET(vbox));
	open_button->act_on_release = 1;
	exec_button = button_create("exec", ptylist_exec_button, ptylist,
	    "exec", WIDGET(vbox));
	exec_button->act_on_release = 1;
	XResizeWindow(DPY(dpy), ptylist->context_menu->window,
	    MAX(
	        WIDGET(open_button)->prefer_size[0],
	        WIDGET(exec_button)->prefer_size[0]),
	    font_height() * 2);
	widget_show(ptylist->context_menu);
}

static void
ptylist_ptyaction(struct pty *pty, PtyAction ptyaction, const char *s,
    int x, int y, void *udata)
{
	struct ptylist *ptylist = udata;
	extern struct dpy *dpy;
	XEvent e;

	switch (ptyaction) {
	case PtyActionOpen:
		ptylist_context_open(ptylist, pty, s, x, y);

		XGrabPointer(DPY(dpy), ptylist->context_menu->window, False,
		    ButtonPressMask | ButtonReleaseMask | ButtonMotionMask,
		    GrabModeAsync, GrabModeSync,
		    None, None, CurrentTime);

		XSync(DPY(dpy), False);
		event_dispatch_xevents(1);

		XMaskEvent(DPY(dpy), ButtonReleaseMask, &e);

		if (e.xbutton.button == 3 &&
		    e.xbutton.window == ptylist->context_menu->window &&
		    e.xbutton.subwindow != 0) {
			e.xbutton.window = e.xbutton.subwindow;
			XSendEvent(DPY(dpy), e.xbutton.subwindow,
			    True, ButtonReleaseMask, &e);

		} else
			ptylist_context_close(ptylist);

		XUngrabPointer(DPY(dpy), CurrentTime);
		event_dispatch_xevents(1);
		break;
	case PtyActionClose:
		ptylist_close_pty(ptylist, pty);
		break;
	case PtyActionToggleHide:
		pty_toggle_hide_output(pty);
		break;
	}
}

static void
ptylist_close_pty(struct ptylist *ptylist, struct pty *pty)
{
	struct widget *root, *ptywidget;
	int i;

	root = widget_find_root(WIDGET(ptylist));

	/*
	 * TODO: Remove this hack. Focusing cmd_editor here is needed
	 * only because the rest of this function knows only the keyboard
	 * focus and keyboard focus can be only on an editor, while in
	 * fact what we care here is the pty itself. We get pty pointer
	 * when we're handling mouse presses e.g. to pty's buttons.
	 */
	if (pty != NULL) {
		widget_focus(WIDGET(pty->cmd_editor));
	}

	ptywidget = root->focus;

	/*
	 * Ensure focus can land on a new pty, refuse otherwise.
	 */
	widget_focus_prev(ptywidget, root->level);
	if (root->focus == ptywidget) {
		widget_focus_next(ptywidget, root->level);
		if (root->focus == ptywidget) {
			widget_focus(ptywidget);
			return;
		}
	}

	/*
	 * Remove it.
	 */

	i = ptylist_find_pty(ptylist, ptywidget);
	if (i >= 0) {
		pty_free(ptylist->ptys[i]);
		if (i+1 < ptylist->n_ptys)
			memmove(&ptylist->ptys[i],
			    &ptylist->ptys[i+1],
			    (ptylist->n_ptys-i-1) *
			    sizeof(struct pty *));
		ptylist->n_ptys--;
	} else
		assert(0);
}

static struct pty *
ptylist_add_pty(struct ptylist *ptylist, struct pty *master)
{
	int i;
	struct widget *root, *ptywidget = NULL;
	char name[256];
	struct pty *pty;

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
	pty = pty_create(master, name, WIDGET(ptylist->vbox));
	if (pty == NULL) {
		warn("creating pty");
		return NULL;
	}
	pty_set_action_callback(pty, ptylist_ptyaction, ptylist);
	ptylist->ptys[i] = pty;
	ptylist->n_ptys++;

	if (ptywidget != NULL)
		widget_move_after(WIDGET(ptylist->ptys[i]), ptywidget);
	widget_focus(WIDGET(ptylist->ptys[i]));

	return pty;
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

struct pty *
ptylist_find_focus(struct ptylist *ptylist)
{
	struct widget *root, *ptywidget;
	int i;

	root = widget_find_root(WIDGET(ptylist));
	ptywidget = root->focus;

	i = ptylist_find_pty(ptylist, ptywidget);
	if (i >= 0)
		return ptylist->ptys[i];

	return NULL;
}

static void
ptylist_focus_change(int state, void *udata)
{
	struct ptylist *ptylist = udata;
	struct pty *pty;

	pty = ptylist_find_focus(ptylist);
	if (pty != NULL)
		editor_shrink(pty->ts_editor);
}

static void
ptylist_create_new_window()
{
	struct ptylist *np;
	XEvent e;
	extern struct dpy *dpy;
	char buf[16];

	snprintf(buf, sizeof(buf), "vtsh%d", ++ptylist_i);

	np = ptylist_create(buf, NULL);
	np->next = ptylist_root->first;
	ptylist_root->first = np;
	XSync(DPY(dpy), False);
	do {
		XMaskEvent(DPY(dpy), StructureNotifyMask, &e);
	} while (e.type != MapNotify);
}

void
ptylist_toggle_focus_level(struct ptylist *ptylist)
{
	struct widget *root;

	root = widget_find_root(WIDGET(ptylist));
	root->level ^= 1;
	if (root->level == 0)
		widget_focus_prev(root->focus, root->level);
	else
		widget_focus_next(root->focus, root->level);
}

static int
ptylist_keypress(XKeyEvent *xkey, void *udata)
{
	struct ptylist *ptylist = udata;
	KeySym sym;
	int i;
	struct pty *pty;
	extern struct dpy *dpy;

	sym = XkbKeycodeToKeysym(DPY(ptylist->dpy), xkey->keycode, 0,
	    (xkey->state & ShiftMask) ? 1 : 0);

	if (sym == XK_s && xkey->state & ControlMask) {
		pty = ptylist_find_focus(ptylist);
		pty_save(pty);
		warnx("saved");
	}

	if (!(xkey->state & Mod1Mask) && sym != XK_Escape)
		return 0;

	switch (sym) {
	case XK_n:
		ptylist_create_new_window();
		return 1;		
	case XK_space:
	case XK_Insert:
		pty = ptylist_add_pty(ptylist, NULL);
		return 1;
	case XK_s:
		pty = ptylist_find_focus(ptylist);
		if (pty != NULL) {
			if (pty->ptyfd != -1)
				ptylist_add_pty(ptylist, pty);
			else if (pty->master != NULL)
				ptylist_add_pty(ptylist, pty->master);
		}
		return 1;
	case XK_H:
		pty = ptylist_find_focus(ptylist);
		pty_show_output(pty);
		for (i = 0; i < ptylist->n_ptys; i++) {
			if (ptylist->ptys[i] == pty)
				continue;
			pty_hide_output(ptylist->ptys[i]);
		}
		return 1;
	case XK_h:
		pty = ptylist_find_focus(ptylist);
		if (pty != NULL)
			pty_toggle_hide_output(pty);
		return 1;
	case XK_Escape:
	case XK_Return:
		ptylist_toggle_focus_level(ptylist);
		return 1;
	case XK_BackSpace:
		ptylist_close_pty(ptylist, NULL);
		return 1;
	}

	return 0;
}
