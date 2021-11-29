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

#include "editor.h"
#include "buffer.h"
#include "font.h"
#include "color.h"
#include "dpy.h"
#include "xevent.h"
#include "util.h"
#include "widget.h"

#include <stdio.h>

#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>
#include <X11/keysymdef.h>
#include <X11/keysym.h>

#include <string.h>
#include <stdlib.h>
#include <assert.h>

static char	*get_line_at_cursor(struct cursor *, int);
static void	 editor_draw(struct editor *, size_t, size_t);
static int	 editor_scroll_into_view(struct editor *, size_t, size_t);
static void	 editor_scroll_down(struct editor *, size_t);
static void	 editor_scroll_up(struct editor *, size_t);
static void	 editor_expose(int, int, int, int, void *);
static void	 editor_focus(int focused, void *);
static int	 editor_keypress(XKeyEvent *, void *);
static int	 editor_draw_cursor(struct editor *, struct cursor *, int);
static void	 editor_update_geometry(void *);

static XIC ic;

static void
editor_update_geometry(void *udata)
{
	struct editor *editor = udata;
	int rows;

	font_set(FONT_NORMAL);
	rows = WIDGET(editor)->height / font_height();
	if (rows <= 0)
		rows = 1;

	editor->bottom_row = editor->top_row + rows - 1;
}

static int
editor_scroll_into_view(struct editor *editor, size_t row, size_t col)
{
	size_t d;

	if (row > editor->bottom_row) {
		d = row - editor->bottom_row;
		editor->top_row += d;
		editor->bottom_row += d;
		editor_scroll_down(editor, d);
		XFlush(DPY(editor->dpy));
		return 1;
	} else if (row < editor->top_row) {
		d = editor->top_row - row;
		editor->top_row -= d;
		editor->bottom_row -= d;
		editor_scroll_up(editor, d);
		XFlush(DPY(editor->dpy));
		return 1;
	} else {
		return 0;
	}

	/* TODO: Not reached */
	editor_draw(editor, editor->top_row, editor->bottom_row);
	XFlush(DPY(editor->dpy));
	return 1;
}

int
editor_max_height(struct editor *editor)
{
	font_set(FONT_NORMAL);

	if (editor->max_rows != -1) {
		return MIN(
		    buffer_rows(editor->buffer) * font_height(),
		    editor->max_rows * font_height());
	} else {
		return buffer_rows(editor->buffer) * font_height();
	}
}

static int
editor_draw_cursor(struct editor *editor, struct cursor *cursor, int clear)
{
	int x, y;
	wchar_t ch;
	struct buffer *buffer;
	XGlyphInfo extents;
	size_t len;
	static char dst[4096];

	if (cursor->col > 0)
		len = buffer_u8str_at(editor->buffer, cursor->row, 0,
		    cursor->col-1, dst, sizeof(dst));
	else
		len = 0;

	font_set(FONT_NORMAL);
	font_set_fgcolor(COLOR_TEXT_FG);
	if (editor->focused && !clear) {
		if (cursor == editor->ocursor)
			font_set_bgcolor(COLOR_TEXT_OUTPUT_CURSOR);
		else
			font_set_bgcolor(COLOR_TEXT_CURSOR);
	} else
		font_set_bgcolor(editor->bgcolor);

	buffer = cursor->buffer;

	x = 100;
	y = (cursor->row - editor->top_row) * font_height();
	ch = buffer_at(buffer, cursor->row, cursor->col);
	if (ch == '\0')
		ch = ' ';

	font_extents(dst, len, &extents);
	x += extents.xOff;

	font_draw_wc(editor->window, x, y, &ch, 1);
	XFlush(DPY(editor->dpy));

	return x;
}

static void
editor_focus(int focused, void *udata)
{
	struct editor *editor = udata;

	editor->focused = focused;
	if (editor->ocursor)
		editor_draw_cursor(editor, editor->ocursor, 0);
	editor_draw_cursor(editor, editor->cursor, 0);
	XFlush(DPY(editor->dpy));
}

void
draw_update(
	int row,
	int col,
	int to_row,
	int to_col,
	BufferUpdate type,
	void *udata)
{
	struct editor *ctx = udata;

	if (ctx->old_height != editor_max_height(ctx)) {
		ctx->old_height = editor_max_height(ctx);
		WIDGET(ctx)->prefer_height = editor_max_height(ctx);
		widget_update_geometry(WIDGET(ctx));
	}

	/* TODO: Bring back autoscroll by re-enabling this. */
#if 0
	/*
	 * If we didn't scroll, we need to draw from here and we draw
	 * just the modified line, nothing else.
	 */
	if (editor_scroll_into_view(ctx, to_row, to_col) == 0)
		editor_draw(ctx, row, to_row);
#else
	editor_draw(ctx, row, to_row);
#endif

	/*
	 * Flush is required because the update might have been triggered
	 * from a pty event. Flush is otherwise called automatically for
	 * all X11 event sequences.
	 */
	XFlush(DPY(ctx->dpy));
}

void
editor_set_cursor(struct editor *editor, struct cursor *cursor,
	struct cursor *ocursor)
{
	XClearWindow(DPY(editor->dpy), editor->window);
	editor->cursor = cursor;
	editor->buffer = cursor->buffer;
	editor->ocursor = ocursor;
	buffer_add_listener(editor->buffer, draw_update, editor);
	XFlush(DPY(editor->dpy));
}

void
editor_set_resize_handler(struct editor *editor, EditResizeHandler resize,
	void *udata)
{
	editor->resize = resize;
	editor->resize_udata = udata;
}

struct editor *
editor_create(struct dpy *dpy, struct cursor *cursor, EditSubmitHandler submit,
	void *udata, int bgcolor, int max_rows, const char *name,
	struct widget *parent)
{
	XIM xim;
	struct editor *editor;

	if ((editor = calloc(1, sizeof(struct editor))) == NULL)
		return NULL;
	editor->dpy = dpy;

	editor->widget = widget_create(name, parent);
	editor->widget->can_focus = 1;

	widget_set_focus_change_callback(WIDGET(editor), editor_focus,
	    editor);
	widget_set_keypress_callback(WIDGET(editor), editor_keypress,
	    editor);

	editor->gc = XCreateGC(DPY(dpy), WINDOW(editor), 0, NULL);

	editor->window = WINDOW(editor);

	widget_set_geometry_callback(WIDGET(editor), editor_update_geometry,
	    editor);

	editor->widget->prefer_height = 28 * 1;

	editor->buffer = cursor->buffer;
	editor->cursor = cursor;
	editor->submit = submit;
	editor->submit_udata = udata;
	editor->bgcolor = bgcolor;
	editor->max_rows = max_rows;

	buffer_add_listener(cursor->buffer, draw_update, editor);

	editor_draw_cursor(editor, cursor, 0);

	xim = XOpenIM(DPY(dpy), NULL, NULL, NULL);

	ic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	    XNClientWindow, WINDOW(editor), NULL);

#if 0
	XSetICFocus(ic);
#endif

	widget_set_draw_callback(WIDGET(editor), editor_expose, editor);

	widget_show(WIDGET(editor));

	return editor;
}

void
editor_free(struct editor *editor)
{
	/* TODO: free resources */
	buffer_remove_listener(editor->buffer, draw_update);
	free(editor);
}

static char *
get_line_at_cursor(struct cursor *cursor, int begin_col)
{
	static char buf[4096];
	size_t len;

	len = buffer_u8str_at(cursor->buffer, cursor->row, begin_col, -1,
	    buf, sizeof(buf));
	buf[len] = '\0';

	return buf;
}

static int
editor_keypress(XKeyEvent *e, void *udata)
{
	struct editor *vc = udata;
	KeySym sym;
	unsigned char ch[4 + 1];
	int n;

	sym = XkbKeycodeToKeysym(DPY(vc->dpy), e->keycode, 0,
	    (e->state & ShiftMask) ? 1 : 0);

	if (e->state & Mod1Mask)
		return 0;

	switch (sym) {
	case XK_Left:
	case XK_Right:
	case XK_Up:
	case XK_Down:
	case XK_Page_Up:
	case XK_Page_Down:
		editor_draw_cursor(vc, vc->cursor, 1);
		if (vc->ocursor) {
			if (vc->cursor->row == vc->ocursor->row &&
			    vc->cursor->col == vc->ocursor->col)
				editor_draw_cursor(vc, vc->ocursor, 0);
		}
		break;
	}

	switch (sym) {
	case XK_KP_Enter:
	case XK_Return:
		if (vc->submit != NULL) {
			if (vc->ocursor)
				vc->submit(get_line_at_cursor(vc->cursor,
				    vc->ocursor->col), vc->submit_udata);
			else
				vc->submit(get_line_at_cursor(vc->cursor, 0),
				    vc->submit_udata);
		} else {
			buffer_insert(vc->cursor, "\n", 1);
		}
		return 1;
	case XK_Left:
		buffer_update_cursor(vc->buffer, vc->cursor, 0, -1);
		break;
	case XK_Right:
		buffer_update_cursor(vc->buffer, vc->cursor, 0, 1);
		break;
	case XK_Up:
		buffer_update_cursor(vc->buffer, vc->cursor, -1, 0);
		break;
	case XK_Down:
		buffer_update_cursor(vc->buffer, vc->cursor, 1, 0);
		break;
	case XK_Page_Up:
		buffer_update_cursor(vc->buffer, vc->cursor,
		    -(1 + (vc->bottom_row - vc->top_row)), 0);
		break;
	case XK_Page_Down:
		buffer_update_cursor(vc->buffer, vc->cursor,
		    1 + (vc->bottom_row - vc->top_row), 0);
		break;
	case XK_BackSpace:
		if (vc->cursor->col > 0) {
			vc->cursor->col--;
			buffer_erase(vc->buffer, vc->cursor);
		}
		return 1;
	case XK_Delete:
		return 1;
	}

	switch (sym) {
	case XK_Left:
	case XK_Right:
	case XK_Up:
	case XK_Down:
	case XK_Page_Up:
	case XK_Page_Down:
		editor_scroll_into_view(vc, vc->cursor->row, vc->cursor->col);
		if (vc->ocursor && (vc->cursor->row != vc->ocursor->row ||
		    vc->cursor->col != vc->ocursor->col))
			editor_draw_cursor(vc, vc->ocursor, 0);
		editor_draw_cursor(vc, vc->cursor, 0);
		return 1;
	}

	if ((n = Xutf8LookupString(ic, e, ch, sizeof(ch), &sym, NULL)) < 0) {
		/* TODO: Handle this error case */
		n = 0;
	} else {
		buffer_insert(vc->cursor, ch, n);
		return 1;
	}

	return 0;
}

static void
editor_scroll_down(struct editor *editor, size_t steps)
{
	/*
	 * Move previous contents up, draw bottom
	 */
	if (steps * font_height() < WIDGET(editor)->height) {
		XCopyArea(DPY(editor->dpy), editor->window, editor->window,
		    editor->gc, 0, steps * font_height(),
		    WIDGET(editor)->width, WIDGET(editor)->height - (steps * font_height()),
		    0, 0);
		editor_draw(editor, editor->bottom_row - (steps-1),
		    editor->bottom_row);
	} else {
		editor_draw(editor, editor->top_row, editor->bottom_row);
	}
}

static void
editor_scroll_up(struct editor *editor, size_t steps)
{
	/*
	 * Move previous contents down, draw up
	 */
	if (steps * font_height() < WIDGET(editor)->height) {
		XCopyArea(DPY(editor->dpy), editor->window, editor->window,
		    editor->gc, 0, 0,
		    WIDGET(editor)->width, WIDGET(editor)->height -
		    (steps * font_height()), 0, steps * font_height());
		editor_draw(editor, editor->top_row,
		    editor->top_row + (steps-1));
	} else {
		editor_draw(editor, editor->top_row, editor->bottom_row);
	}
}

static void
editor_draw(struct editor *editor, size_t from, size_t to)
{
	size_t i, x, y, len;
	size_t rows;
	char lineno[256];
	static char dst[4096];

	font_set(FONT_NORMAL);
	font_set_fgcolor(COLOR_TEXT_FG);

	rows = buffer_rows(editor->buffer);

	font_set_bgcolor(COLOR_TEXT_LINENO);
	for (i = from; i <= to; i++) {
		if (i < editor->top_row)
			continue;
		if (i > editor->bottom_row)
			continue;

		x = 0;
		y = (i - editor->top_row) * font_height();

		snprintf(lineno, sizeof(lineno), "%03zu", i + 1);
		font_draw(editor->window, x, y, lineno, strlen(lineno));
	}

	font_set_bgcolor(editor->bgcolor);
	for (i = from; i <= to && i < rows; i++) {
		if (i < editor->top_row)
			continue;
		if (i > editor->bottom_row)
			continue;

		len = buffer_u8str_at(editor->buffer, i, 0, -1, dst,
		    sizeof(dst));
		x = 100;
		y = (i - editor->top_row) * font_height();

		x += font_draw(editor->window, x, y, dst, len);
		font_clear(editor->window, x, y, WIDGET(editor)->width - x);
	}

	if (editor->ocursor)
		editor_draw_cursor(editor, editor->ocursor, 0);
	editor_draw_cursor(editor, editor->cursor, 0);
	XFlush(DPY(editor->dpy));
}

static void
editor_expose(int x, int y, int width, int height, void *udata)
{
	struct editor *editor = udata;

	editor_draw(editor, editor->top_row, editor->bottom_row);
}
