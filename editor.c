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
#include "uflags.h"

#include <stdio.h>

#include <X11/XKBlib.h>
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
static int	 editor_mousepress(XButtonEvent *, void *);
static int	 editor_draw_cursor(struct editor *, struct cursor *, int);
static void	 editor_update_geometry(void *);
static void	 editor_find_cursor_pos(struct editor *, XButtonEvent *,
		    int *, int *);
static void	 editor_page_up(struct editor *);
static void	 editor_page_down(struct editor *);

static void
editor_update_geometry(void *udata)
{
	struct editor *editor = udata;
	int rows;

	font_set(FONT_NORMAL);
	rows = WIDGET_HEIGHT(editor) / font_height();
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

	if (editor->max_rows != -1)
		return MIN(
		    buffer_rows(editor->buffer) * font_height(),
		    editor->max_rows * font_height());
	else {
		return MAX(buffer_rows(editor->buffer) * font_height(),
		    font_height());
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

	if (editor->focused == focused)
		return;

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
		WIDGET_PREFER_HEIGHT(ctx) = editor_max_height(ctx);
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
	widget_set_mousepress_callback(WIDGET(editor), editor_mousepress,
	    editor);

	editor->gc = XCreateGC(DPY(dpy), WINDOW(editor), 0, NULL);

	editor->window = WINDOW(editor);

	widget_set_geometry_callback(WIDGET(editor), editor_update_geometry,
	    editor);

	font_set(FONT_NORMAL);
	WIDGET_PREFER_HEIGHT(editor) = font_height();
	WIDGET_PREFER_WIDTH(editor) = 9999;

	editor->buffer = cursor->buffer;
	editor->cursor = cursor;
	editor->submit = submit;
	editor->submit_udata = udata;
	editor->bgcolor = bgcolor;
	editor->max_rows = max_rows;

	buffer_add_listener(cursor->buffer, draw_update, editor);

	editor_draw_cursor(editor, cursor, 0);

	widget_set_draw_callback(WIDGET(editor), editor_expose, editor);

	widget_show(WIDGET(editor));

	return editor;
}

void
editor_free(struct editor *editor)
{
	extern struct dpy *dpy;

	buffer_remove_listener(editor->buffer, draw_update);
	if (editor->gc)
		XFreeGC(DPY(dpy), editor->gc);
	widget_free(WIDGET(editor));
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

static void
editor_find_cursor_pos(struct editor *editor, XButtonEvent *e, int *row,
    int *col)
{
	XGlyphInfo extents;
	int x;
	wchar_t ch;

	*row = e->y / font_height();
	*row += editor->top_row;

	x = 100;
	*col = -1;
	do {
		ch = buffer_at(editor->buffer, *row, ++(*col));
		if (ch == '\0')
			ch = ' ';
		font_extents_wc(&ch, 1, &extents);
		x += extents.xOff;
	} while (x < e->x);
}

static void
editor_page_up(struct editor *vc)
{
	int rows, page;

	rows = WIDGET_HEIGHT(vc) / font_height();
	page = vc->cursor->row / rows;

	if (vc->cursor->row != page * rows) {
		vc->top_row = page * rows;
	} else if (page > 0) {
		page--;
		vc->top_row = page * rows;
	}

	assert(rows >= 1);
	vc->bottom_row = vc->top_row + (rows-1);

	buffer_set_cursor(vc->buffer, vc->cursor, vc->top_row, 0);
	editor_draw(vc, vc->top_row, vc->bottom_row);
}

static void
editor_page_down(struct editor *vc)
{
	int rows, page;

	rows = WIDGET_HEIGHT(vc) / font_height();
	page = vc->cursor->row / rows;

	if (vc->cursor->row != page * rows + (rows-1)) {
		vc->top_row = page * rows;
	} else if ((page+1) * rows < buffer_rows(vc->buffer)) {
		page++;
		vc->top_row = page * rows;
	}

	assert(rows >= 1);
	vc->bottom_row = vc->top_row + (rows-1);

	buffer_set_cursor(vc->buffer, vc->cursor, vc->bottom_row, 0);
	editor_draw(vc, vc->top_row, vc->bottom_row);
}

static int
editor_mousepress(XButtonEvent *e, void *udata)
{
	struct editor *editor = udata;
	int row, col;

	widget_focus(WIDGET(editor));

	switch (e->button) {
	case 1:
		editor_draw_cursor(editor, editor->cursor, 1);
		editor_find_cursor_pos(editor, e, &row, &col);
		editor->cursor->row = row;
		editor->cursor->col = col;
		editor_draw_cursor(editor, editor->cursor, 0);
		return 1;
	case 4:
		editor_page_up(editor);
		return 1;
	case 5:
		editor_page_down(editor);
		return 1;
	}

	return 1;
}

static int
editor_keypress(XKeyEvent *e, void *udata)
{
	struct editor *vc = udata;
	KeySym sym;
	char ch[4 + 1];
	int n;
	int row, col;
	char *line;
	size_t len;
	int diff;

	sym = XkbKeycodeToKeysym(DPY(vc->dpy), e->keycode, 0,
	    (e->state & ShiftMask) ? 1 : 0);

	if (e->state & Mod1Mask || sym == XK_Escape)
		return 0;

	if (e->state & ControlMask) {
		switch (sym) {
		case XK_a:
			buffer_update_cursor(vc->buffer, vc->cursor, 0,
			    -vc->cursor->col);
			return 1;
		case XK_e:
			buffer_update_cursor(vc->buffer, vc->cursor, 0,
			    buffer_cols(vc->buffer, vc->cursor->row) -
			    vc->cursor->col);
			return 1;
		case XK_k:
			if (buffer_cols(vc->buffer, vc->cursor->row) == 0)
				buffer_remove_row(vc->buffer, vc->cursor->row);
			else
				buffer_erase_eol(vc->buffer, vc->cursor);
			return 1;
		case XK_b:
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -1);
			return 1;
		case XK_f:
			buffer_update_cursor(vc->buffer, vc->cursor, 0, 1);
			return 1;
		case XK_p:
			buffer_update_cursor(vc->buffer, vc->cursor, -1, 0);
			return 1;
		case XK_n:
			buffer_update_cursor(vc->buffer, vc->cursor, 1, 0);
			return 1;
		case XK_d:
			buffer_delete_char(vc->buffer, vc->cursor);
			return 1;
		case XK_o:
			row = vc->cursor->row;
			col = vc->cursor->col;
			buffer_insert(vc->cursor, "\n", 1);
			vc->cursor->row = row;
			vc->cursor->col = col;
			editor_draw_cursor(vc, vc->cursor, 0);
			return 1;
		case XK_l:
			diff = vc->cursor->row -
			    ((vc->top_row + vc->bottom_row) / 2);
			if (vc->top_row + diff <= 0)
				return 1;

			vc->top_row += diff;
			vc->bottom_row += diff;
			if (diff < 0)
				editor_scroll_up(vc, -diff);
			else
				editor_scroll_down(vc, diff);
			return 1;
		}
	}

	switch (sym) {
	case XK_Left:
	case XK_Right:
	case XK_Up:
	case XK_Down:
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
			if (vc->ocursor) {
				vc->submit(get_line_at_cursor(vc->cursor,
				    0), vc->submit_udata);
			} else {
				row = vc->cursor->row;
				col = vc->cursor->col;
				line = get_line_at_cursor(vc->cursor, 0);
				len = strlen(line);

				if (row+1 == buffer_rows(vc->buffer)) {
					buffer_insert(vc->cursor, "\n", 1);
					vc->cursor->row = row;
					vc->cursor->col = col;
				
					editor_scroll_into_view(vc,
					    vc->cursor->row, vc->cursor->col);
				}
				vc->submit(line, vc->submit_udata);
			}
		} else {
			buffer_insert(vc->cursor, "\n", 1);
		}
		return 1;
	case XK_Left:
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -8);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -1);
		break;
	case XK_Right:
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, 0, 8);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, 0, 1);
		break;
	case XK_Up:
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, -8, 0);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, -1, 0);
		break;
	case XK_Down:
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, 8, 0);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, 1, 0);
		break;
	case XK_Page_Up:
		editor_page_up(vc);
		return 1;
	case XK_Page_Down:
		editor_page_down(vc);
		return 1;
	case XK_BackSpace:
		buffer_erase(vc->buffer, vc->cursor);
		return 1;
	case XK_Delete:
		return 1;
	}

	switch (sym) {
	case XK_Left:
	case XK_Right:
	case XK_Up:
	case XK_Down:
		editor_scroll_into_view(vc, vc->cursor->row, vc->cursor->col);
		if (vc->ocursor && (vc->cursor->row != vc->ocursor->row ||
		    vc->cursor->col != vc->ocursor->col))
			editor_draw_cursor(vc, vc->ocursor, 0);
		editor_draw_cursor(vc, vc->cursor, 0);
		return 1;
	}

	if ((n = Xutf8LookupString(WIDGET(vc)->ic, e, ch, sizeof(ch),
	    &sym, NULL)) < 0) {
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
	if (steps * font_height() < WIDGET_HEIGHT(editor)) {
		XCopyArea(DPY(editor->dpy), editor->window, editor->window,
		    editor->gc, 0, steps * font_height(),
		    WIDGET_WIDTH(editor), WIDGET_HEIGHT(editor) -
		    (steps * font_height()), 0, 0);
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
	if (steps * font_height() < WIDGET_HEIGHT(editor)) {
		XCopyArea(DPY(editor->dpy), editor->window, editor->window,
		    editor->gc, 0, 0,
		    WIDGET_WIDTH(editor), WIDGET_HEIGHT(editor) -
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

		if (buffer_row_uflags(editor->buffer, i) & ROW_UFLAGS_CMDLINE)
			font_set_fgcolor(COLOR_TEXT_CURSOR);
		else
			font_set_fgcolor(COLOR_TEXT_FG);

		snprintf(lineno, sizeof(lineno), "%zu", i + 1);
		x += font_draw(editor->window, x, y, lineno, strlen(lineno));
		font_clear(editor->window, x, y, 100 - x);
	}

	font_set_bgcolor(editor->bgcolor);
	font_set_fgcolor(COLOR_TEXT_FG);
	for (i = from; i <= to; i++) {
		if (i < editor->top_row)
			continue;
		if (i > editor->bottom_row)
			continue;

		x = 100;
		y = (i - editor->top_row) * font_height();
		if (i < rows) {
			len = buffer_u8str_at(editor->buffer, i, 0, -1, dst,
			    sizeof(dst));

			x += font_draw(editor->window, x, y, dst, len);
			font_clear(editor->window, x, y,
			    WIDGET_WIDTH(editor) - x);
		}
		font_clear(editor->window, x, y, WIDGET_WIDTH(editor) - x);
	}

	if (editor->ocursor)
		editor_draw_cursor(editor, editor->ocursor, 0);

	editor_draw_cursor(editor, editor->cursor, 0);

	/*
	 * TODO: Do this only when necessary e.g. clear the remaining
	 *       area that was too small for a full height text line at
	 *       the bottom. It is not necessary when only updating one
	 *       line.
	 */
	y = (WIDGET_HEIGHT(editor) / font_height()) * font_height();
	if (y < WIDGET_HEIGHT(editor))
		XClearArea(DPY(editor->dpy), WIDGET(editor)->window, 0,
		    y, WIDGET_WIDTH(editor), WIDGET_HEIGHT(editor) - y, False);

	XFlush(DPY(editor->dpy));
}

static void
editor_expose(int x, int y, int width, int height, void *udata)
{
	struct editor *editor = udata;

	editor_draw(editor, editor->top_row, editor->bottom_row);
}
