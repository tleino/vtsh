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
#include "event.h"
#include "util.h"
#include "config.h"
#include "font.h"

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <err.h>

#ifdef DEBUG
#include <stdio.h>
#endif

typedef enum FocusDir {
	FOCUS_FORWARD,
	FOCUS_BACKWARD
} FocusDir;

static int		 widget_find_child(struct widget *, struct widget *);
static int		 widget_add_child(struct widget *, struct widget *);
static int		 widget_remove_child(struct widget *, struct widget *);

static void		 widget_expose(XExposeEvent *, void *);
static void		 widget_resize(XConfigureEvent *, void *);
static void		 widget_keypress(XKeyEvent *, void *);
static void		 widget_mousepress(XButtonEvent *, void *);
static void		 widget_motion(XMotionEvent *, void *);

static int		 widget_root_keypress(XKeyEvent *, void *);

static int		 widget_ensure_focus(struct widget *);
static void		 widget_focus_dir(struct widget *, FocusDir, int);
static struct widget	*widget_find_focusable(struct widget *, FocusDir,
			    int *, struct widget *, int);

static struct widget	*_widget_create(int, int, unsigned long, const char *,
			    struct widget *);

static void		 widget_notify_focus_change(struct widget *, int);

static int		 widget_enable_protocols(struct widget *);
static void		 widget_enable_hints(struct widget *);
static void		 widget_takefocus(Time, void *);

static void		 widget_root_idle(void *);

static void		 widget_flush_expose(struct widget *);
static void		 widget_flush_changes(struct widget *);

#ifdef DEBUG
static void		 widget_dump_tree(struct widget *, int);
#else
#define widget_dump_tree(...) ((void) 0)
#endif

#ifdef DEBUG
void
widget_print_name(struct widget *widget)
{
	if (widget->parent)
		widget_print_name(widget->parent);

	printf("%s", widget->name);
	if (widget->nchildren != 0)
		putchar('/');
}
#endif

static void
widget_expose(XExposeEvent *e, void *udata)
{
	struct widget *widget = udata;

	widget->expose_from_px = MIN(widget->expose_from_px, e->y);
	widget->expose_to_px = MAX(widget->expose_to_px, (e->y + e->height));
	widget->need_expose = 1;
	widget->need_expose_from_event = 1;
#ifdef DEBUG
	printf("Expose ");
	widget_print_name(widget);
	printf(" (%d->%d)\n", widget->expose_from_px, widget->expose_to_px);
#endif
}

static void
widget_update_prefer(struct widget *widget)
{
	if (widget->update_prefer != NULL)
		widget->update_prefer(widget->update_prefer_udata);
}

static void
widget_call_geometry(struct widget *widget)
{
	int i;

	if (widget->parent != NULL) {
		if (!widget->was_hidden) {
			widget->old_size[WIDTH_AXIS] =
			    widget->size[WIDTH_AXIS];
			widget->old_size[HEIGHT_AXIS] =
			    widget->size[HEIGHT_AXIS];
			widget->old_pos[WIDTH_AXIS] = widget->pos[WIDTH_AXIS];
			widget->old_pos[HEIGHT_AXIS] =
			    widget->pos[HEIGHT_AXIS];
			widget->old_physical_size[WIDTH_AXIS] =
			    widget->physical_size[WIDTH_AXIS];
			widget->old_physical_size[HEIGHT_AXIS] =
			    widget->physical_size[HEIGHT_AXIS];
		}

		if (!widget->has_managed_geometry) {
			widget->size[WIDTH_AXIS] =
			    widget->parent->size[WIDTH_AXIS];
			widget->size[HEIGHT_AXIS] =
			    widget->parent->size[HEIGHT_AXIS];
			widget->pos[WIDTH_AXIS] =
			    widget->parent->pos[WIDTH_AXIS];
			widget->pos[HEIGHT_AXIS] =
			    widget->parent->pos[HEIGHT_AXIS];
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
	 *
	 * FIXME: Why this was needed in the first place?
	 *        Apparently it causes problems when a hidden widget
	 *        reappears, so disabling it for now.
	 */
#if 0
	if (widget->window != 0 && widget->parent != NULL &&
	    (widget->old_size[WIDTH_AXIS] != widget->size[WIDTH_AXIS] ||
	    widget->old_size[HEIGHT_AXIS] != widget->size[HEIGHT_AXIS] ||
	    widget->old_pos[WIDTH_AXIS] != widget->pos[WIDTH_AXIS] ||
	    widget->old_pos[HEIGHT_AXIS] != widget->pos[HEIGHT_AXIS])) {
		XMoveResizeWindow(DPY(dpy), widget->window,
		    widget->pos[WIDTH_AXIS], widget->pos[HEIGHT_AXIS],
		    widget->size[WIDTH_AXIS], widget->size[HEIGHT_AXIS]);
	}
#endif

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
	extern int running;

	sym = XkbKeycodeToKeysym(DPY(dpy), xkey->keycode, 0,
	    (xkey->state & ShiftMask) ? 1 : 0);

	if (xkey->state & Mod1Mask) {
		switch (sym) {
		case XK_Up:
			widget_focus_prev(widget->focus, widget->level);
			return 1;
		case XK_Down:
			widget_focus_next(widget->focus, widget->level);
			return 1;
		case XK_q:
			running = 0;
			return 1;
#ifdef DEBUG
		case XK_a:
			puts("REQUESTED DUMP TREE");
			widget_dump_tree(widget, 0);
			return 1;
#endif
		}
	}

	return 0;
}

static void
widget_mousepress(XButtonEvent *xbutton, void *udata)
{
	struct widget *orig, *widget = udata;
	int ret;

	orig = widget;
	while (widget != NULL) {
		if (widget->mousepress != NULL &&
		    (ret = widget->mousepress(orig, xbutton,
		    widget->mousepress_udata)) == 1)
			break;
		widget = widget->parent;
	}
}

static void
widget_motion(XMotionEvent *xmotion, void *udata)
{
	struct widget *widget = udata;

	if (widget->motion)
		widget->motion(xmotion, widget->motion_udata);
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

static void
widget_flush_expose(struct widget *widget)
{
	int i;
	int from, to;

	if (widget->need_expose && widget->draw != NULL) {
		from = MAX(widget->expose_from_px, 0);
		to = MAX(widget->expose_to_px, 0);
		from = MIN(from, widget->size[1]);
		to = MIN(to, widget->size[1]);

		widget->need_expose = 0;
		widget->expose_from_px = 0;
		widget->expose_to_px = 0;

		if (from != to) {
#ifdef DEBUG
			printf("\tDraw ");
			widget_print_name(widget);
			printf(" (%d->%d) (event=%d)\n", from, to,
			    widget->need_expose_from_event);
#endif
			widget->draw(0, from, widget->size[0], to-from,
			    widget->draw_udata);
		}

		widget->need_expose_from_event = 0;
	}

	for (i = 0; i < widget->nchildren; i++)
		widget_flush_expose(widget->children[i]);
}

static void
widget_flush_changes(struct widget *widget)
{
	extern struct dpy *dpy;
	int i;

	if (widget->changes_mask != 0 && widget->window != 0) {
#ifdef DEBUG
		printf("\tChange ");
		widget_print_name(widget);
		if (widget->changes_mask & CWY)
			printf(" (y->%d)", widget->changes.y);
		if (widget->changes_mask & CWX)
			printf(" (x->%d)", widget->changes.x);
		if (widget->changes_mask & CWWidth)
			printf(" (w->%d)", widget->changes.width);
		if (widget->changes_mask & CWHeight)
			printf(" (h->%d)", widget->changes.height);
		printf("\n");
#endif
		XConfigureWindow(DPY(dpy), widget->window,
		    widget->changes_mask, &widget->changes);
		widget->changes_mask = 0;
	}

#ifdef WANT_FLUSHES_IN_REVERSE
	for (i = widget->nchildren-1; i >= 0; i--)
		widget_flush_changes(widget->children[i]);
#else
	for (i = 0; i < widget->nchildren; i++)
		widget_flush_changes(widget->children[i]);
#endif
}

/*
 * Should be called only for the top-level window.
 */
static void
widget_resize(XConfigureEvent *e, void *udata)
{
	struct widget *widget = udata;
	extern struct dpy *dpy;

	widget->size[WIDTH_AXIS] = e->width;
	widget->size[HEIGHT_AXIS] = e->height;

	widget_call_geometry(widget);

#ifdef DEBUG
	printf("widget_resize to %d,%d\n", e->width, e->height);
#endif
}

#ifdef DEBUG
static void
widget_dump_tree(struct widget *widget, int depth)
{
	int i;

	for (i = 0; i < depth; i++)
		putchar('\t');
	printf("%s (prefer %d,%d actual %d,%d pos %d,%d vis %d)\n",
	    widget->name, widget->prefer_size[0], widget->prefer_size[1],
	    widget->size[0], widget->size[1], widget->pos[0], widget->pos[1],
	    widget->visible);

	for (i = 0; i < widget->nchildren; i++)
		widget_dump_tree(widget->children[i], depth+1);
}
#endif

void
widget_update_geometry(struct widget *widget)
{
	while (widget && widget->parent != NULL) {
		widget = widget->parent;
		widget_update_prefer(widget);
	}

	widget_call_geometry(widget);
}

static void
widget_notify_focus_change(struct widget *widget, int state)
{
	if (widget->parent)
		widget_notify_focus_change(widget->parent, state);

	if (widget->focus_change != NULL)
		widget->focus_change(state, widget->focus_change_udata);
}

void
widget_focus(struct widget *widget)
{
	struct widget *root;
	int has_prev;

	assert(widget != NULL);

	has_prev = 1;
	widget = widget_find_focusable(widget, FOCUS_FORWARD, &has_prev,
	    NULL, -1);
	if (widget == NULL)
		return;

	root = widget_find_root(widget);
	if (root->focus != NULL) {
		root->focus->has_focus = 0;
		widget_notify_focus_change(root->focus, 0);
	}

	root->focus = widget;
	root->level = widget->level;
	root->focus->has_focus = 1;

	XSetICFocus(root->focus->ic);

	widget_notify_focus_change(root->focus, 1);
}

static struct widget *
widget_find_focusable(struct widget *widget, FocusDir dir, int *has_prev,
    struct widget *prevfocus, int level)
{
	int i;
	struct widget *child;

	switch (dir) {
	case FOCUS_FORWARD:
		for (i = 0; i < widget->nchildren; i++) {
			child = widget->children[i];
			child = widget_find_focusable(child, dir,
			    has_prev, prevfocus, level);
			if (child != NULL)
				return child;
		}
		break;
	case FOCUS_BACKWARD:
		for (i = widget->nchildren - 1; i >= 0; i--) {
			child = widget->children[i];
			child = widget_find_focusable(child, dir,
			    has_prev, prevfocus, level);
			if (child != NULL)
				return child;

		}
		break;
	}

	if (widget->can_focus) {
		if (widget == prevfocus)
			*has_prev = 1;
		else if (*has_prev == 1 && widget->visible &&
		    (widget->level == level || level == -1))
			return widget;
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
	    NULL, -1);
	if (newfocus == NULL)
		return 0;

	widget_focus(newfocus);
	return 1;
}

static void
widget_focus_dir(struct widget *widget, FocusDir dir, int level)
{
	struct widget *newfocus, *root;
	int has_prev = 0;

	root = widget_find_root(widget);
	newfocus = widget_find_focusable(root, dir, &has_prev, widget, level);
	if (newfocus == NULL)
		return;
	widget_focus(newfocus);
}

void
widget_focus_next(struct widget *widget, int level)
{
	widget_focus_dir(widget, FOCUS_FORWARD, level);
}

void
widget_focus_prev(struct widget *widget, int level)
{
	widget_focus_dir(widget, FOCUS_BACKWARD, level);
}

void
widget_set_keypress_callback(struct widget *widget, WidgetKeyPress keypress,
	void *udata)
{
	widget->keypress = keypress;
	widget->keypress_udata = udata;

	if (widget->window != 0) {
		widget->ic = XCreateIC(widget_find_root(widget)->xim,
		    XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
		    XNClientWindow, widget->window, NULL);
		if (widget->ic == NULL)
			errx(1, "cannot create Input Context");
	}
}

void
widget_set_focus_change_callback(struct widget *widget,
	WidgetFocusChange focus_change, void *udata)
{
	widget->focus_change = focus_change;
	widget->focus_change_udata = udata;
}

void
widget_set_mousepress_callback(struct widget *widget,
	WidgetMousePress mousepress, void *udata)
{
	widget->mousepress = mousepress;
	widget->mousepress_udata = udata;
}

void
widget_set_motion_callback(struct widget *widget,
	WidgetMotion motion, void *udata)
{
	widget->motion = motion;
	widget->motion_udata = udata;
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
widget_set_geometry_callback(struct widget *widget, WidgetGeometry geometry,
    void *udata)
{
	widget->geometry = geometry;
	widget->geometry_udata = udata;
}

void
widget_set_update_prefer_callback(struct widget *widget,
    WidgetUpdatePrefer prefer, void *udata)
{
	widget->update_prefer = prefer;
	widget->update_prefer_udata = udata;
}

struct widget *
widget_create_windowless(const char *name, struct widget *parent)
{
	extern struct dpy *dpy;

	return _widget_create(1, 0, BlackPixel(DPY(dpy), DPY_SCREEN(dpy)),
	    name, parent);
}

struct widget *
widget_create(const char *name, struct widget *parent)
{
	extern struct dpy *dpy;

	return _widget_create(0, 0, BlackPixel(DPY(dpy), DPY_SCREEN(dpy)),
	    name, parent);
}

struct widget *
widget_create_transient(const char *name, struct widget *parent)
{
	extern struct dpy *dpy;

	return _widget_create(0, 1, BlackPixel(DPY(dpy), DPY_SCREEN(dpy)),
	    name, parent);
}

struct widget *
widget_create_colored(unsigned long bgcolor, const char *name,
    struct widget *parent)
{
	return _widget_create(0, 0, bgcolor, name, parent);
}

static struct widget *
_widget_create(int windowless, int transient, unsigned long bgcolor,
    const char *name, struct widget *parent)
{
	struct widget *widget;
	Window parent_window;
	extern struct dpy *dpy;
	XSetWindowAttributes a;
	unsigned long v;

	if ((widget = calloc(1, sizeof(struct widget))) == NULL)
		return NULL;

	widget->name = strdup(name);

	if (parent == NULL || transient) {
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

		widget->xim = XOpenIM(DPY(dpy), NULL, NULL, NULL);
		if (widget->xim == NULL)
			errx(1, "no XIM");

		add_idle_handler(widget_root_idle, widget);
	} else {
		widget_add_child(parent, widget);
		parent_window = widget_find_parent_window(widget)->window;
	}

	if (parent != NULL && !transient) {
		widget->pos[WIDTH_AXIS] = parent->pos[WIDTH_AXIS];
		widget->pos[HEIGHT_AXIS] = parent->pos[HEIGHT_AXIS];
		widget->size[WIDTH_AXIS] = parent->size[WIDTH_AXIS];
		widget->size[HEIGHT_AXIS] = parent->size[HEIGHT_AXIS];
	} else {
		widget->pos[WIDTH_AXIS] = 0;
		widget->pos[HEIGHT_AXIS] = 0;
		widget->size[WIDTH_AXIS] = 640;
		widget->size[HEIGHT_AXIS] = 480;
	}

	if (windowless)
		return widget;

	v = 0;

	widget->event_mask |= ButtonPressMask;
	widget->event_mask |= ButtonReleaseMask;
	widget->event_mask |= Button1MotionMask;
	a.event_mask = widget->event_mask;
	v |= CWEventMask;

	a.background_pixel = bgcolor;
	v |= CWBackPixel;

	if (dpy->backing_store != NotUseful) {
		a.backing_store = WhenMapped;
		v |= CWBackingStore;
	}

	if (transient) {
		a.override_redirect = True;
		v |= CWOverrideRedirect;
	}

	/*
	 * Setting 'bit_gravity' would be nice for avoiding repaint
	 * flicker, but it causes drawing glitches when resizing/moving
	 * windows in quick succession.
	 */
#if 0
	a.bit_gravity = StaticGravity;
	v |= CWBitGravity;
#endif

	widget->window = XCreateWindow(DPY(dpy), parent_window,
	    widget->pos[WIDTH_AXIS], widget->pos[HEIGHT_AXIS],
	    widget->size[WIDTH_AXIS], widget->size[HEIGHT_AXIS],
	    0, CopyFromParent, InputOutput, CopyFromParent,
	    v, &a);

	if (transient) {
		assert(parent != NULL);
		XSetTransientForHint(DPY(dpy), widget->window, parent->window);
	}

	XStoreName(DPY(dpy), widget->window, name);

	if (widget->event_mask & StructureNotifyMask)
		add_resize_handler(widget->window, widget_resize, widget);
	if (widget->event_mask & KeyPressMask)
		add_keypress_handler(widget->window, widget_keypress, widget);
	if (widget->event_mask & ButtonPressMask)
		add_button_handler(widget->window, widget_mousepress, widget);
	if (widget->event_mask & Button1MotionMask)
		add_motion_handler(widget->window, widget_motion, widget);

	widget_ensure_focus(widget);

	if (widget->parent == NULL) {
		widget_enable_protocols(widget);
		widget_enable_hints(widget);
	}

	return widget;
}

struct widget *
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

	/*
	 * TODO: We need to make size/pos bogus so that the flush_changes
	 *       algorithm notices a change when we do update_geometry.
	 *       However, we need this only in a special case when we have
	 *       shown a widget, but did not complete flush_changes before
	 *       hiding the widget again, i.e. these bogus values could be
	 *       even removed once fixing hide/show logic.
	 */
	widget->old_size[0] = 9999;
	widget->old_size[1] = 9999;
	widget->old_pos[0] = 0;
	widget->old_pos[1] = 0;

	widget->was_hidden = 1;
	widget_update_geometry(widget);	
	widget->was_hidden = 0;

	widget_flush_changes(widget);

	if (widget->window != 0)
		XMapWindow(DPY(dpy), widget->window);
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

void
widget_move_after(struct widget *widget, struct widget *after)
{
	void *dst, *src;
	int i;

	assert(widget != NULL);
	assert(widget->parent != NULL);
	assert(after != NULL);
	assert(widget->parent == after->parent);
	assert(widget->parent->nchildren > 0);

	i = widget_find_child(widget->parent, widget);
	assert(i >= 0);

	/* Shrink */
	if (i+1 < widget->parent->nchildren) {
		dst = &widget->parent->children[i];
		src = &widget->parent->children[i+1];
		memmove(dst, src, (widget->parent->nchildren-i-1) *
		    sizeof(void *));
	}
	widget->parent->nchildren--;

	i = widget_find_child(widget->parent, after);
	assert(i >= 0);
	i++;

	/* Expand */
	dst = &widget->parent->children[i+1];
	src = &widget->parent->children[i];
	widget->parent->nchildren++;
	memmove(dst, src,
	    (widget->parent->nchildren-(i+1)) * sizeof(void *));

	/* Put */
	widget->parent->children[i] = widget;

	widget_update_geometry(widget->parent);
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
	void *src, *dst;

	if ((i = widget_find_child(widget, child)) == -1)
		return -1;

	if (i+1 < widget->nchildren) {
		dst = &widget->children[i];
		src = &widget->children[i+1];
		memmove(dst, src, (widget->nchildren-i-1) * sizeof(void *));
	}
	widget->nchildren--;
	return 0;
}

static void
widget_enable_hints(struct widget *widget)
{
	XWMHints hints = { 0 };
	extern struct dpy *dpy;

	hints.flags |= InputHint;
	hints.flags |= StateHint;
	hints.input = True;
	hints.initial_state = NormalState;
	XSetWMHints(DPY(dpy), widget->window, &hints);
}

static int
widget_enable_protocols(struct widget *widget)
{
	Atom atoms[2];
	extern struct dpy *dpy;

	add_focus_handler(widget->window, widget_takefocus, widget);

	atoms[0] = XInternAtom(DPY(dpy), "WM_TAKE_FOCUS", False);
	atoms[1] = XInternAtom(DPY(dpy), "WM_DELETE_WINDOW", False);

	if (!XSetWMProtocols(DPY(dpy), widget->window, atoms, 2))
		return -1;
	return 0;
}

static void
widget_takefocus(Time t, void *udata)
{
	struct widget *widget = udata;
	extern struct dpy *dpy;

#ifdef DEBUG
	printf("TAKEFOCUS\n");
#endif

	XSetInputFocus(DPY(dpy), widget->window, RevertToNone, t);
}

static void
widget_root_idle(void *udata)
{
	struct widget *widget = udata;
	extern struct dpy *dpy;

#ifdef DEBUG
	printf("widget_root_idle %s\n", widget->name);
#endif

	widget_flush_changes(widget);
	widget_flush_expose(widget);

	XFlush(DPY(dpy));
}

void
widget_free(struct widget *widget)
{
	extern struct dpy *dpy;

	if (widget->window != 0)
		remove_handlers_for_window(widget->window);

	if (widget->parent == NULL)
		remove_idle_handler(widget_root_idle, widget);

	widget_hide(widget);

	while (widget->nchildren > 0)
		widget_free(widget->children[widget->nchildren-1]);

	if (widget->children != NULL) {
		free(widget->children);
		widget->children = NULL;
	}

	if (widget->parent != NULL)
		widget_remove_child(widget->parent, widget);
	
	if (widget->window != 0 && widget->event_mask & KeyPressMask &&
	    widget->ic != 0)
		XDestroyIC(widget->ic);

	if (widget->parent == NULL && widget->xim != NULL)
		XCloseIM(widget->xim);

	if (widget->focus == widget)
		widget->focus = NULL;

	font_destroy_ftdraw();

	if (widget->window != 0)
		XDestroyWindow(DPY(dpy), widget->window);

	if (widget->name != NULL)
		free(widget->name);

	free(widget);
}
