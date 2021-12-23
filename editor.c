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
#include "config.h"

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
static int	 editor_motion(XMotionEvent *, void *);
static int	 editor_draw_cursor(struct editor *, struct cursor *, int);
static void	 editor_update_geometry(void *);
static void	 editor_find_cursor_pos(struct editor *, int, int,
		    int *, int *);
static void	 editor_page_up(struct editor *);
static void	 editor_page_down(struct editor *);
static int	 editor_row_is_visible(struct editor *, int);
static int	 editor_offset_from_col(struct editor *, int, int);
static int	 editor_col_from_offset(struct editor *, int, int);
static void	 editor_hscroll(struct editor *, int);
static void	 draw_update(int, int, int, int, BufferUpdate, void *udata);

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
		return 1;
	} else if (row < editor->top_row) {
		d = editor->top_row - row;
		editor->top_row -= d;
		editor->bottom_row -= d;
		editor_scroll_up(editor, d);
		return 1;
	}

	return 0;
}

void
editor_shrink(struct editor *editor)
{
	editor->largest_height = buffer_rows(editor->buffer) * font_height();
	editor->old_height = editor_max_height(editor);
	WIDGET_PREFER_HEIGHT(editor) = editor_max_height(editor);
	widget_update_geometry(WIDGET(editor));
}

int
editor_max_height(struct editor *editor)
{
	int height;

	font_set(FONT_NORMAL);

	height = MAX(
	    buffer_rows(editor->buffer) * font_height(),
	    editor->largest_height);
	if (height > editor->largest_height)
		editor->largest_height = height;

	if (editor->max_rows != -1)
		return MIN(
		    height,
		    editor->max_rows * font_height());
	else {
		return MAX(height,
		    font_height());
	}
}

static int
editor_draw_cursor(struct editor *editor, struct cursor *cursor, int clear)
{
	int x, y;
	wchar_t ch;
	struct buffer *buffer;
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
	x -= editor->begin_offset;
	y = (cursor->row - editor->top_row) * font_height();
	ch = buffer_at(buffer, cursor->row, cursor->col);
	if (ch == '\0')
		ch = ' ';

	x += font_str_width(x-100, dst, len);

	font_draw_wc(editor->window, x, y, &ch, 1);
	return x;
}

static void
editor_focus(int focused, void *udata)
{
	struct editor *editor = udata;

	if (editor->focused == focused)
		return;

	editor->focused = focused;
	editor_scroll_into_view(editor, editor->cursor->row,
	    editor->cursor->col);

	if (editor->ocursor)
		editor_draw_cursor(editor, editor->ocursor, 0);
	editor_draw_cursor(editor, editor->cursor, 0);
}

/*
 * Returns cursor column position from pixel x-offset.
 */
static int
editor_col_from_offset(struct editor *editor, int row, int offset)
{
	size_t i;
	XGlyphInfo extents;
	size_t cols;
	wchar_t ch;
	int x;

	font_set(FONT_NORMAL);
	cols = buffer_cols(editor->buffer, row);
	x = 0;
	for (i = 0; i < cols && x < offset; i++) {
		ch = buffer_at(editor->buffer, row, i);
		font_extents_wc(&ch, 1, &extents);
		x += extents.xOff;
	}
	return i;
}

/*
 * Returns pixel x-offset for cursor position.
 */
static int
editor_offset_from_col(struct editor *editor, int row, int col)
{
	static char dst[4096];
	size_t len, offset;

	if (col > 0)
		len = buffer_u8str_at(editor->buffer, row, 0, col-1, dst,
		    sizeof(dst)-1);
	else
		len = 0;

	font_set(FONT_NORMAL);
	offset = font_str_width(0, dst, len);
	return offset;
}

/*
 * Scroll view horizontally when needed.
 */
static void
editor_hscroll(struct editor *editor, int dir)
{
	int x, boundary, add;

	x = editor_offset_from_col(editor, editor->cursor->row,
	    editor->cursor->col);
	boundary = WIDGET_WIDTH(editor) - WIDGET_WIDTH(editor) / 3;
	add = WIDGET_WIDTH(editor) / 3;
	if (dir == 1 && x - editor->begin_offset < boundary)
		return;
	if (dir == -1 && x - editor->begin_offset > (boundary - add))
		return;

	switch (dir) {
	case 1:
		editor->begin_offset += add;
		break;
	case -1:
		if (editor->begin_offset > add)
			editor->begin_offset -= add;
		else
			editor->begin_offset = 0;
		break;
	}

	draw_update(editor->top_row, 0, editor->bottom_row, 0,
	    BUFFER_UPDATE_LINE, editor);
}

static void
draw_update(
	int row,
	int col,
	int to_row,
	int to_col,
	BufferUpdate type,
	void *udata)
{
	struct editor *ctx = udata;
	int row_px, to_row_px;

	row_px = (row - ctx->top_row) * font_height();
	to_row_px = (to_row - ctx->top_row + 1) * font_height();

	if (WIDGET(ctx)->need_expose == 0) {
		WIDGET(ctx)->expose_from_px = row_px;
		WIDGET(ctx)->expose_to_px = to_row_px;
		WIDGET(ctx)->need_expose = 1;
	} else {
		WIDGET(ctx)->expose_from_px = MIN(row_px,
		    WIDGET(ctx)->expose_from_px);
		WIDGET(ctx)->expose_to_px = MAX(to_row_px,
		    WIDGET(ctx)->expose_to_px);
	}

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
#endif
}

void
editor_set_cursor(struct editor *editor, struct cursor *cursor,
	struct cursor *ocursor)
{
	XClearWindow(DPY(editor->dpy), editor->window);

	editor->cursor = cursor;
	editor->buffer = cursor->buffer;
	editor->ocursor = ocursor;
	editor->top_row = 0;
	editor->bottom_row = 0;
	
	buffer_add_listener(editor->buffer, draw_update, editor);
}

void
editor_set_resize_handler(struct editor *editor, EditResizeHandler resize,
	void *udata)
{
	editor->resize = resize;
	editor->resize_udata = udata;
}

static void
editor_prompt_submit(const char *s, void *udata)
{
	struct editor *editor = udata;
	int i, rows;
	int val;
	static char dst[4096];
	size_t n;

	switch (editor->prompt_action) {
	case PROMPT_ACTION_GOTO:
		val = atoi(s);
		if (val <= 0)
			return;
		val--;
		buffer_set_cursor(editor->buffer, editor->cursor, val, 0);
		editor_scroll_into_view(editor, editor->cursor->row,
		    editor->cursor->col);
		break;
	case PROMPT_ACTION_FSEARCH:
		rows = buffer_rows(editor->buffer);
		for (i = editor->cursor->row; i < rows; i++) {
			n = buffer_u8str_at(editor->buffer, i, 0, -1,
			    dst, sizeof(dst)-1);
			dst[n] = '\0';
			if (strstr(dst, s) != NULL)
				break;
		}
		if (i == rows)
			for (i = editor->cursor->row; i >= 0; i--) {
				n = buffer_u8str_at(editor->buffer, i, 0, -1,
				    dst, sizeof(dst)-1);
				dst[n] = '\0';
				if (strstr(dst, s) != NULL)
					break;
			}

		if (i != rows) {
			buffer_set_cursor(editor->buffer, editor->cursor, i,
			    0);
			editor_scroll_into_view(editor, editor->cursor->row,
			    editor->cursor->col);
		}
		break;
	case PROMPT_ACTION_RSEARCH:
		rows = buffer_rows(editor->buffer);
		for (i = editor->cursor->row; i >= 0; i--) {
			n = buffer_u8str_at(editor->buffer, i, 0, -1,
			    dst, sizeof(dst)-1);
			dst[n] = '\0';
			if (strstr(dst, s) != NULL)
				break;
		}
		if (i == rows)
			for (i = editor->cursor->row; i < rows; i++) {
				n = buffer_u8str_at(editor->buffer, i, 0, -1,
				    dst, sizeof(dst)-1);
				dst[n] = '\0';
				if (strstr(dst, s) != NULL)
					break;
			}

		if (i != rows) {
			buffer_set_cursor(editor->buffer, editor->cursor, i,
			    0);
			editor_scroll_into_view(editor, editor->cursor->row,
			    editor->cursor->col);
		}
		break;
	default:
		assert(0);
	}

	if (editor->prompt) {
		buffer_clear_row(editor->prompt_buffer, 0);
		widget_hide(WIDGET(editor->prompt));
		widget_focus(WIDGET(editor));
	}
}

struct editor *
editor_create(struct dpy *dpy, struct cursor *cursor, EditSubmitHandler submit,
	void *udata, int bgcolor, int max_rows, int no_prompt,
	const char *name, struct widget *parent)
{
	struct editor *editor;

	if ((editor = calloc(1, sizeof(struct editor))) == NULL)
		return NULL;
	editor->dpy = dpy;

	editor->widget = widget_create_colored(
	    query_color(dpy, bgcolor).pixel, name, parent);
	editor->widget->can_focus = 1;

	widget_set_focus_change_callback(WIDGET(editor), editor_focus,
	    editor);
	widget_set_keypress_callback(WIDGET(editor), editor_keypress,
	    editor);
	widget_set_mousepress_callback(WIDGET(editor), editor_mousepress,
	    editor);
	widget_set_motion_callback(WIDGET(editor), editor_motion, editor);

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

	if (!no_prompt) {
		editor->prompt_buffer = buffer_create();
		if (editor->prompt_buffer)
			editor->prompt_cursor =
			    buffer_cursor_create(editor->prompt_buffer);
		if (editor->prompt_cursor)
			editor->prompt = editor_create(dpy,
			    editor->prompt_cursor, editor_prompt_submit,
			    editor, COLOR_TITLE_FG_NORMAL, 1, 1,
			    "prompt", parent);
		if (editor->prompt) {
			editor->prompt->prompt_parent = editor;
			WIDGET(editor->prompt)->level = 1;
			WIDGET_PREFER_WIDTH(editor->prompt) = 9999;
			widget_hide(WIDGET(editor->prompt));
		}
	}

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
editor_find_cursor_pos(struct editor *editor, int ex, int ey, int *row,
    int *col)
{
	int x;
	wchar_t ch;

	*row = ey / font_height();
	*row += editor->top_row;

	x = 100;
	*col = -1;
	do {
		ch = buffer_at(editor->buffer, *row, ++(*col));
		if (ch == '\0')
			ch = ' ';
		x += font_str_width_wc(x-100, &ch, 1);
	} while (x < ex);
}

static int
editor_row_is_visible(struct editor *vc, int row)
{
	if (row >= vc->top_row && row <= vc->bottom_row)
		return 1;

	return 0;
}

static void
editor_page_up(struct editor *vc)
{
	int rows, page;

	rows = WIDGET_HEIGHT(vc) / font_height();
	page = vc->cursor->row / rows;

	if (vc->cursor->row != page * rows &&
	    editor_row_is_visible(vc, page * rows)) {
		buffer_set_cursor(vc->buffer, vc->cursor, page * rows, 0);
	} else {
		if (page > 0)
			page--;

		vc->top_row = page * rows;
		assert(rows > 0);
		vc->bottom_row = vc->top_row + (rows-1);
		buffer_set_cursor(vc->buffer, vc->cursor, vc->top_row, 0);
	}

	editor_draw(vc, vc->top_row, vc->bottom_row);
}

static void
editor_page_down(struct editor *vc)
{
	int rows, page, bottom;

	rows = WIDGET_HEIGHT(vc) / font_height();
	page = vc->cursor->row / rows;

	assert(rows > 0);
	bottom = page * rows + (rows-1);
	if (bottom >= buffer_rows(vc->buffer))
		bottom = buffer_rows(vc->buffer)-1;

	if (vc->cursor->row != bottom && editor_row_is_visible(vc, bottom)) {
		buffer_set_cursor(vc->buffer, vc->cursor, bottom, 0);
	} else if ((page+1) * rows < buffer_rows(vc->buffer)) {
		page++;
		vc->top_row = page * rows;
		vc->bottom_row = vc->top_row + (rows-1);
		buffer_set_cursor(vc->buffer, vc->cursor, vc->bottom_row, 0);
	}

	editor_draw(vc, vc->top_row, vc->bottom_row);
}

static int
editor_motion(XMotionEvent *e, void *udata)
{
	struct editor *editor = udata;
	int row, col;

	editor_draw_cursor(editor, editor->cursor, 1);
	editor_find_cursor_pos(editor, e->x, e->y, &row, &col);
	buffer_set_cursor(editor->buffer, editor->cursor, row, col);
	editor_scroll_into_view(editor, editor->cursor->row,
	    editor->cursor->col);
	return 1;
}

static int
editor_mousepress(XButtonEvent *e, void *udata)
{
	struct editor *editor = udata;
	int row, col;

	if (e->type == ButtonRelease)
		return 0;

	widget_focus(WIDGET(editor));

	switch (e->button) {
	case 1:
		editor_draw_cursor(editor, editor->cursor, 1);
		editor_find_cursor_pos(editor, e->x, e->y, &row, &col);
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
	int diff;

	sym = XkbKeycodeToKeysym(DPY(vc->dpy), e->keycode, 0,
	    (e->state & ShiftMask) ? 1 : 0);

	if (e->state & Mod1Mask || sym == XK_Escape)
		return 0;

	if (e->state & ControlMask && vc->x_on) {
		vc->x_on = 0;
		switch (sym) {
		case XK_s:	/* bubble up */
			return 0;
		case XK_g:
			if (vc->prompt != NULL) {
				vc->prompt_action = PROMPT_ACTION_GOTO;
				widget_show(WIDGET(vc->prompt));
				widget_focus(WIDGET(vc->prompt));
			}
			return 1;
		}
	} else if (sym == XK_x && e->state & ControlMask) {
		vc->x_on = 1;
		return 1;
	} else {
		vc->x_on = 0;
	}

	if (e->state & ControlMask) {
		switch (sym) {
		case XK_g:
			if (vc->prompt_parent != NULL) {
				vc->prompt_action = PROMPT_ACTION_NONE;
				widget_hide(WIDGET(vc));
				widget_focus(WIDGET(vc->prompt_parent));
			}
			return 1;
		case XK_s:
			if (vc->prompt != NULL) {
				vc->prompt_action = PROMPT_ACTION_FSEARCH;
				widget_show(WIDGET(vc->prompt));
				widget_focus(WIDGET(vc->prompt));
			}
			return 1;
		case XK_r:
			if (vc->prompt != NULL) {
				vc->prompt_action = PROMPT_ACTION_RSEARCH;
				widget_show(WIDGET(vc->prompt));
				widget_focus(WIDGET(vc->prompt));
			}
			return 1;
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
		if (vc->submit != NULL)
			vc->submit(get_line_at_cursor(vc->cursor,
			    0), vc->submit_udata);
		else
			buffer_insert(vc->cursor, "\n", 1);
		return 1;
	case XK_Left:
		editor_hscroll(vc, -1);
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -8);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -1);
		break;
	case XK_Right:
		editor_hscroll(vc, 1);
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
		editor_hscroll(vc, -1);
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
		return 1;
	}

	if ((n = Xutf8LookupString(WIDGET(vc)->ic, e, ch, sizeof(ch),
	    &sym, NULL)) < 0) {
		/* TODO: Handle this error case */
		n = 0;
	} else {
		editor_hscroll(vc, 1);
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
	int i, x, y, len;
	size_t rows;
#ifdef WANT_LINE_NUMBERS
	char lineno[256];
#endif
	static char dst[4096];

	font_set(FONT_NORMAL);
	font_set_fgcolor(COLOR_TEXT_FG);

	rows = buffer_rows(editor->buffer);

	font_set_bgcolor(editor->bgcolor);
	font_set_fgcolor(COLOR_TEXT_FG);
	for (i = from; i <= to; i++) {
		if (i < editor->top_row)
			continue;
		if (i > editor->bottom_row)
			continue;

		x = 100;
		y = (i - editor->top_row) * font_height();
		x -= editor->begin_offset;

		if (y >= WIDGET_HEIGHT(editor))
			continue;

		if (i < rows) {
			len = buffer_u8str_at(editor->buffer, i, 0, -1, dst,
			    sizeof(dst));

			x += font_draw(editor->window, x, y, dst, len);
			if (WIDGET_WIDTH(editor)-x > 0)
				font_clear(editor->window, x, y,
				    WIDGET_WIDTH(editor) - x);
		}
		if (WIDGET_WIDTH(editor)-x > 0)
			font_clear(editor->window, x, y,
			    WIDGET_WIDTH(editor) - x);
	}

#ifdef WANT_LINE_NUMBERS
	font_set_bgcolor(COLOR_TEXT_LINENO);
	for (i = from; i <= to; i++) {
		if (i < editor->top_row)
			continue;
		if (i > editor->bottom_row)
			continue;

		x = 0;
		y = (i - editor->top_row) * font_height();

		if (y >= WIDGET_HEIGHT(editor))
			continue;

		if (buffer_row_uflags(editor->buffer, i) & ROW_UFLAGS_CMDLINE)
			font_set_fgcolor(COLOR_TEXT_CURSOR);
		else
			font_set_fgcolor(COLOR_TEXT_FG);

		/*
		 * TODO: Implement minimum WIDGET_HEIGHT because this can
		 *       result in floating point exception because this
		 *       can become i % 0.
		 */
		if (i % (WIDGET_HEIGHT(editor) / font_height()) == 0)
			snprintf(lineno, sizeof(lineno), "%d->", i + 1);
		else
			snprintf(lineno, sizeof(lineno), "%d", i + 1);
		x += font_draw(editor->window, x, y, lineno, strlen(lineno));
		font_clear(editor->window, x, y, 100 - x);
	}
#endif

	if (editor->ocursor)
		editor_draw_cursor(editor, editor->ocursor, 0);

	editor_draw_cursor(editor, editor->cursor, 0);

	y = (WIDGET_HEIGHT(editor) / font_height()) * font_height();
	if (y < WIDGET_HEIGHT(editor) && y > WIDGET_HEIGHT(editor) -
	    font_height()) {
		XClearArea(DPY(editor->dpy), WIDGET(editor)->window, 0,
		    y, WIDGET_WIDTH(editor), WIDGET_HEIGHT(editor) - y, False);
	}
}

static void
editor_expose(int x, int y, int width, int height, void *udata)
{
	struct editor *editor = udata;
	int from, to;

	from = editor->top_row + (y / font_height());
	to = editor->top_row + ((y + height) / font_height());
	editor_draw(editor, from, to);
}
