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

#ifndef WIDGET_H
#define WIDGET_H

#include <X11/Xlib.h>

struct widget;

typedef void (*WidgetDraw)(int, int, int, int, void *);
typedef int (*WidgetKeyPress)(XKeyEvent *, void *);
typedef int (*WidgetMousePress)(struct widget *, XButtonEvent *, void *);
typedef int (*WidgetMotion)(XMotionEvent *, void *);
typedef void (*WidgetFocusChange)(int, void *);
typedef void (*WidgetUpdatePrefer)(void *);
typedef void (*WidgetGeometry)(void *);

#define WIDGET(_x) (_x)->widget
#define WINDOW(_x) WIDGET((_x))->window
#define NCHILDREN(_x) WIDGET((_x))->nchildren
#define CHILD(_x, _y) WIDGET((_x))->children[(_y)]

#define WIDTH_AXIS 0
#define HEIGHT_AXIS 1

#define WIDGET_HEIGHT(_x) WIDGET((_x))->size[HEIGHT_AXIS]
#define WIDGET_WIDTH(_x) WIDGET((_x))->size[WIDTH_AXIS]

#define WIDGET_PREFER_HEIGHT(_x) \
	WIDGET((_x))->prefer_size[HEIGHT_AXIS]
#define WIDGET_PREFER_WIDTH(_x) \
	WIDGET((_x))->prefer_size[WIDTH_AXIS]

#define WIDTH(_x) (_x)->size[WIDTH_AXIS]
#define HEIGHT(_x) (_x)->size[HEIGHT_AXIS]
#define POSX(_x) (_x)->pos[WIDTH_AXIS]
#define POSY(_x) (_x)->pos[HEIGHT_AXIS]
#define OLD_WIDTH(_x) (_x)->old_size[WIDTH_AXIS]
#define OLD_HEIGHT(_x) (_x)->old_size[HEIGHT_AXIS]
#define OLD_POSX(_x) (_x)->old_pos[WIDTH_AXIS]
#define OLD_POSY(_x) (_x)->old_pos[HEIGHT_AXIS]
#define PREFER_WIDTH(_x) (_x)->size[WIDTH_AXIS]
#define PREFER_HEIGHT(_x) (_x)->size[HEIGHT_AXIS]

struct widget {
	Window window;

	char *name;

	struct widget *parent;
	struct widget **children;
	int nchildren;

	int level;
	int has_focus;
	int can_focus;

	WidgetDraw draw;
	void *draw_udata;

	WidgetGeometry geometry;
	void *geometry_udata;

	WidgetUpdatePrefer update_prefer;
	void *update_prefer_udata;

	WidgetKeyPress keypress;
	void *keypress_udata;

	WidgetMousePress mousepress;
	void *mousepress_udata;

	WidgetMotion motion;
	void *motion_udata;

	WidgetFocusChange focus_change;
	void *focus_change_udata;

	/*
	 * Actual geometry of the widget.
	 */
	int size[2];
	int pos[2];

	int old_size[2];
	int old_pos[2];

	int physical_size[2];
	int old_physical_size[2];

	/*
	 * 'prefer_' are hints for layout management.
	 */
	int prefer_size[2];

	int has_managed_geometry;

	int visible;
	int was_hidden;

	long event_mask;

	XIC ic;

	XWindowChanges changes;
	unsigned int changes_mask;

	int need_expose;
	int need_expose_from_event;
	int expose_from_px;
	int expose_to_px;

	/*
	 * These are only relevant in root widget:
	 */
	struct widget *focus;
	XIM xim;
};

struct widget	*widget_create(const char *, struct widget *);
struct widget	*widget_create_windowless(const char *, struct widget *);
struct widget	*widget_create_colored(unsigned long, const char *,
		    struct widget *);
struct widget	*widget_create_transient(const char *, struct widget *);
void		 widget_free(struct widget *);

void		 widget_show(struct widget *);
void		 widget_hide(struct widget *);

struct widget	*widget_find_parent_with_window(struct widget *);
struct widget	*widget_find_parent_window(struct widget *);
struct widget   *widget_find_root(struct widget *);

void		 widget_focus_next(struct widget *, int);
void		 widget_focus_prev(struct widget *, int);
void		 widget_focus(struct widget *);

void		 widget_update_geometry(struct widget *);

void		 widget_move_after(struct widget *, struct widget *);

void		 widget_set_draw_callback(struct widget *,
		    WidgetDraw, void *);
void		 widget_set_geometry_callback(struct widget *,
		    WidgetGeometry, void *);
void		 widget_set_update_prefer_callback(struct widget *,
		    WidgetUpdatePrefer, void *);
void		 widget_set_keypress_callback(struct widget *,
		    WidgetKeyPress, void *);
void		 widget_set_mousepress_callback(struct widget *,
		    WidgetMousePress, void *);
void		 widget_set_motion_callback(struct widget *,
		    WidgetMotion, void *);
void		 widget_set_focus_change_callback(struct widget *,
		    WidgetFocusChange, void *);

#ifdef DEBUG
void		 widget_print_name(struct widget *);
#else
#define widget_print_name(...) ((void) 0)
#endif

#endif
