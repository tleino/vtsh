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

/*
 * widget.c: A simple widget hierarchy system with auxiliary helpers
 */

#include "widget.h"
#include "dpy.h"
#include "xevent.h"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef enum FocusDir {
	FOCUS_FORWARD,
	FOCUS_BACKWARD
} FocusDir;

static int		 widget_find_child(struct widget *, struct widget *);
static int		 widget_add_child(struct widget *, struct widget *);
static int		 widget_remove_child(struct widget *, struct widget *);
static struct widget	*widget_find_root(struct widget *);

static void		 widget_expose(XExposeEvent *, void *);
static void		 widget_resize(XConfigureEvent *, void *);
static void		 widget_keypress(XKeyEvent *, void *);

static int		 widget_root_keypress(XKeyEvent *, void *);

static int		 widget_ensure_focus(struct widget *);
static void		 widget_focus_dir(struct widget *, FocusDir);
static struct widget	*widget_find_focusable(struct widget *, FocusDir,
			    int *, struct widget *);

static struct widget	*_widget_create(int, const char *, struct widget *);

static void		 widget_notify_focus_change(struct widget *, int);

static int		 widget_enable_takefocus(struct widget *);
static void		 widget_takefocus(Time, void *);

static void
widget_expose(XExposeEvent *e, void *udata)
{
	struct widget *widget = udata;

	if (widget->draw != NULL)
		widget->draw(e->x, e->y, e->width, e->height,
		    widget->draw_udata);
}

static void
widget_call_geometry(struct widget *widget)
{
	int i;
	extern struct dpy *dpy;

	if (widget->parent != NULL) {
		widget->old_width = widget->width;
		widget->old_height = widget->height;
		widget->old_x = widget->x;
		widget->old_y = widget->y;

		if (!widget->has_managed_geometry) {
			widget->width = widget->parent->width;
			widget->height = widget->parent->height;
			widget->x = widget->parent->x;
			widget->y = widget->parent->y;
		}
	}

	/*
	 * This may modify geometry, or it may just take track of
	 * changed geometry.
	 */
	if (widget->geometry != NULL)
		widget->geometry(widget->geometry_udata);

	/*
	 * This will get triggered only if we are not in a layout (because
	 * in a layout at this point e.g. 'old_width' will be same as
	 * 'width'.
	 */
	if (widget->window != 0 && widget->parent != NULL &&
	    (widget->old_width != widget->width ||
	    widget->old_height != widget->height ||
	    widget->old_x != widget->x ||
	    widget->old_y != widget->y)) {
		XMoveResizeWindow(DPY(dpy), widget->window, widget->x,
		    widget->y, widget->width, widget->height);
	}

	widget->has_managed_geometry = 0;

	for (i = 0; i < widget->nchildren; i++)
		widget_call_geometry(widget->children[i]);
}

/*
 * Called by widget_keypress when the keypress is not handled by
 * any other widget in the hierarchy because the root widget will
 * have this as a default (overridable) callback.
 */
static int
widget_root_keypress(XKeyEvent *xkey, void *udata)
{
	struct widget *widget = udata;	
	KeySym sym;
	extern struct dpy *dpy;

	sym = XkbKeycodeToKeysym(DPY(dpy), xkey->keycode, 0,
	    (xkey->state & ShiftMask) ? 1 : 0);

	if (xkey->state & Mod1Mask) {
		switch (sym) {
		case XK_Up:
			widget_focus_prev(widget->focus);
			return 1;
		case XK_Down:
			widget_focus_next(widget->focus);
			return 1;
		}
	}

	return 0;
}

/*
 * Should be called only for the top-level window.
 *
 * We first let the focused widget handle the event and if they say
 * they handled it, we stop -- otherwise we "bubble up" and try all
 * widgets that have registered a key handler until finally reaching
 * the top-level widget which will handle some global bindings via
 * a default callback.
 */
static void
widget_keypress(XKeyEvent *xkey, void *udata)
{
	struct widget *widget = udata;
	struct widget *focus;
	int ret;

	assert(widget->parent == NULL);

	widget_ensure_focus(widget);
	focus = widget->focus;
	if (focus == NULL)
		focus = widget;

	while (focus != NULL) {
		if (focus->keypress != NULL &&
		    (ret = focus->keypress(xkey, focus->keypress_udata)) == 1)
			break;
		focus = focus->parent;
	}
	if (focus == NULL && ret == 0)
		widget_root_keypress(xkey, widget);
}

/*
 * Should be called only for the top-level window.
 */
static void
widget_resize(XConfigureEvent *e, void *udata)
{
	struct widget *widget = udata;

	widget->width = e->width;
	widget->height = e->height;

	widget_call_geometry(widget);
}

void
widget_update_geometry(struct widget *widget)
{
	while (widget && widget->parent != NULL)
		widget = widget->parent;

	widget_call_geometry(widget);
}

static void
widget_notify_focus_change(struct widget *widget, int state)
{
	if (widget->focus_change == NULL)
		return;

	widget->focus_change(state, widget->focus_change_udata);
}

void
widget_focus(struct widget *widget)
{
	struct widget *root;

	assert(widget != NULL);
	assert(widget->can_focus == 1);

	root = widget_find_root(widget);
	if (root->focus != NULL) {
		root->focus->has_focus = 0;
		widget_notify_focus_change(root->focus, 0);
	}

	root->focus = widget;
	root->focus->has_focus = 1;
	widget_notify_focus_change(root->focus, 1);
}

static struct widget *
widget_find_focusable(struct widget *widget, FocusDir dir, int *has_prev,
    struct widget *prevfocus)
{
	int i;
	struct widget *child;

	if (widget->nchildren == 0 && widget->can_focus) {
		if (widget == prevfocus)
			*has_prev = 1;
		else if (*has_prev == 1)
			return widget;
	}

	switch (dir) {
	case FOCUS_FORWARD:
		for (i = 0; i < widget->nchildren; i++) {
			child = widget->children[i];
			child = widget_find_focusable(child, dir,
			    has_prev, prevfocus);
			if (child != NULL)
				return child;
		}
		break;
	case FOCUS_BACKWARD:
		for (i = widget->nchildren - 1; i >= 0; i--) {
			child = widget->children[i];
			child = widget_find_focusable(child, dir,
			    has_prev, prevfocus);
			if (child != NULL)
				return child;

		}
		break;
	}
	return NULL;
}

/*
 * Ensure focus is set to some focusable widget. If none is available,
 * returns 0, otherwise 1.
 */
static int
widget_ensure_focus(struct widget *widget)
{
	struct widget *newfocus;
	int has_prev = 1;

	if (widget->focus != NULL)
		return 1;

	if (widget->parent != NULL)
		widget = widget_find_root(widget);

	newfocus = widget_find_focusable(widget, FOCUS_FORWARD, &has_prev,
	    NULL);
	if (newfocus == NULL)
		return 0;

	widget_focus(newfocus);
	return 1;
}

static void
widget_focus_dir(struct widget *widget, FocusDir dir)
{
	struct widget *newfocus, *root;
	int has_prev = 0;

	root = widget_find_root(widget);
	newfocus = widget_find_focusable(root, dir, &has_prev, widget);
	if (newfocus == NULL)
		return;
	widget_focus(newfocus);
}

void
widget_focus_next(struct widget *widget)
{
	widget_focus_dir(widget, FOCUS_FORWARD);
}

void
widget_focus_prev(struct widget *widget)
{
	widget_focus_dir(widget, FOCUS_BACKWARD);
}

void
widget_set_keypress_callback(struct widget *widget, WidgetKeyPress keypress,
	void *udata)
{
	widget->keypress = keypress;
	widget->keypress_udata = udata;
}

void
widget_set_focus_change_callback(struct widget *widget,
	WidgetFocusChange focus_change, void *udata)
{
	widget->focus_change = focus_change;
	widget->focus_change_udata = udata;
}

void
widget_set_draw_callback(struct widget *widget, WidgetDraw draw, void *udata)
{
	extern struct dpy *dpy;

	widget->draw = draw;
	widget->draw_udata = udata;

	if (draw != NULL)
		widget->event_mask |= ExposureMask;
	else
		widget->event_mask &= ~(ExposureMask);

	XSelectInput(DPY(dpy), widget->window, widget->event_mask);
	add_expose_handler(widget->window, widget_expose, widget);
}

void
widget_set_geometry_callback(
	struct widget *widget,
	WidgetGeometry geometry,
	void *udata)
{
	widget->geometry = geometry;
	widget->geometry_udata = udata;
}

struct widget *
widget_create_windowless(const char *name, struct widget *parent)
{
	return _widget_create(1, name, parent);
}

struct widget *
widget_create(const char *name, struct widget *parent)
{
	return _widget_create(0, name, parent);
}

static struct widget *
_widget_create(int windowless, const char *name, struct widget *parent)
{
	struct widget *widget;
	Window parent_window;
	extern struct dpy *dpy;
	XSetWindowAttributes a;
	unsigned long v;

	if ((widget = calloc(1, sizeof(struct widget))) == NULL)
		return NULL;

	widget->name = strdup(name);

	if (parent == NULL) {
		assert(windowless == 0);

		parent_window = DPY_ROOT(dpy);

		widget_set_keypress_callback(widget, widget_root_keypress,
		    widget);

		/*
		 * For top-level windows we wish to receive resize
		 * and keyboard events.
		 *
		 * For other windows receiving these events is not
		 * necessary: we forward the new information as
		 * appropriate through the hierarchy, focus mechanism
		 * and layout containers.
		 */
		widget->event_mask |= (StructureNotifyMask | KeyPressMask);
	} else {
		widget_add_child(parent, widget);
		parent_window = widget_find_parent_window(parent)->window;
	}

	if (parent != NULL) {
		widget->x = parent->x;
		widget->y = parent->y;
		widget->width = parent->width;
		widget->height = parent->height;
	} else {
		widget->x = 0;
		widget->y = 0;
		widget->width = 640;
		widget->height = 480;
	}

	if (windowless)
		return widget;

	a.background_pixel = BlackPixel(DPY(dpy), DPY_SCREEN(dpy));
	a.backing_store = WhenMapped;
	a.event_mask = widget->event_mask;
	v = (CWEventMask | CWBackingStore | CWBackPixel);

	widget->window = XCreateWindow(DPY(dpy), parent_window,
	    widget->x, widget->y, widget->width, widget->height,
	    0, CopyFromParent, InputOutput, CopyFromParent,
	    v, &a);

	XStoreName(DPY(dpy), widget->window, name);

	if (widget->event_mask & StructureNotifyMask)
		add_resize_handler(widget->window, widget_resize, widget);
	if (widget->event_mask & KeyPressMask)
		add_keypress_handler(widget->window, widget_keypress, widget);

	widget_ensure_focus(widget);

	if (widget->parent == NULL)
		widget_enable_takefocus(widget);

	return widget;
}

static struct widget *
widget_find_root(struct widget *widget)
{
	assert(widget != NULL);

	while (widget->parent != NULL)
		widget = widget->parent;

	assert(widget != NULL);
	return widget;
}

struct widget *
widget_find_parent_window(struct widget *widget)
{
	while (widget->parent != NULL) {
		widget = widget->parent;
		if (widget->window != 0)
			break;
	}
	assert(widget != NULL);
	assert(widget->window != 0);
	return widget;
}

void
widget_show(struct widget *widget)
{
	extern struct dpy *dpy;

	if (widget->visible)
		return;

	widget->visible = 1;
	widget_update_geometry(widget);	

	if (widget->window != 0) {
		XRaiseWindow(DPY(dpy), widget->window);
		XMapWindow(DPY(dpy), widget->window);
	}
}

void
widget_hide(struct widget *widget)
{
	extern struct dpy *dpy;

	if (!widget->visible)
		return;

	widget->visible = 0;

	if (widget->window != 0)
		XUnmapWindow(DPY(dpy), widget->window);

	widget_update_geometry(widget);
}

static int
widget_add_child(struct widget *widget, struct widget *child)
{
	struct widget **tmp;

	if ((tmp = realloc(widget->children,
	    (widget->nchildren+1) * sizeof(struct widget **))) == NULL)
		return -1;

	widget->children = tmp;
	widget->children[widget->nchildren] = child;
	widget->nchildren++;
	child->parent = widget;
	return 0;
}

/*
 * Returns -1 if not found.
 */
static int
widget_find_child(struct widget *widget, struct widget *child)
{
	int i;

	for (i = 0; i < widget->nchildren; i++)
		if (widget->children[i] == child)
			return i;

	return -1;
}

/*
 * Returns -1 if not found.
 */
static int
widget_remove_child(struct widget *widget, struct widget *child)
{
	int i;

	if ((i = widget_find_child(widget, child)) == -1)
		return -1;

	widget->nchildren--;

	if (i+1 < widget->nchildren)
		memmove(&widget->children[i], &widget->children[i+1],
		    (widget->nchildren-i) * sizeof(*widget->children));	

	return 0;
}

static int
widget_enable_takefocus(struct widget *widget)
{
	Atom takefocus;
	extern struct dpy *dpy;

	add_focus_handler(widget->window, widget_takefocus, widget);

	takefocus = XInternAtom(DPY(dpy), "WM_TAKE_FOCUS", False);

	if (!XSetWMProtocols(DPY(dpy), widget->window, &takefocus, 1))
		return -1;
	return 0;
}

static void
widget_takefocus(Time t, void *udata)
{
	struct widget *widget = udata;
	extern struct dpy *dpy;

	XSetInputFocus(DPY(dpy), widget->window, RevertToNone, t);
}

void
widget_free(struct widget *widget)
{
	while (widget->nchildren > 0)
		widget_free(widget->children[widget->nchildren-1]);

	if (widget->parent != NULL)
		widget_remove_child(widget->parent, widget);

	if (widget->name != NULL)
		free(widget->name);

	free(widget);
}
