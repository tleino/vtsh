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
#include "utf8.h"

#include <stdio.h>
#include <ctype.h>
#include <math.h>

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
static int	 editor_mousepress(struct widget *, XButtonEvent *, void *);
static int	 editor_motion(XMotionEvent *, void *);
static void	 editor_draw_cursor(struct editor *, struct cursor *);
static void	 editor_update_geometry(void *);
static void	 editor_find_cursor_pos(struct editor *, int, int,
		    int *, int *);
static void	 editor_page_up(struct editor *);
static void	 editor_page_down(struct editor *);
static int	 editor_row_is_visible(struct editor *, int);
static void	 editor_hscroll(struct editor *, int);
static void	 draw_update(int, int, int, int, BufferUpdate, void *udata);
static void	 editor_draw_cursor_now(struct editor *, int);

static int
editor_offset_from_pos(struct editor *editor, int row, int byteoffset,
    size_t *width_at_offset);
static int
editor_pos_from_offset(struct editor *editor, int row, int pxoffset);

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
	size_t width_at_offset, offset;
	int ret, diff, rbound;

	ret = 0;
	if (row > editor->bottom_row) {
		d = row - editor->bottom_row;
		editor->top_row += d;
		editor->bottom_row += d;
		editor_scroll_down(editor, d);
		ret = 1;
	} else if (row < editor->top_row) {
		d = editor->top_row - row;
		editor->top_row -= d;
		editor->bottom_row -= d;
		editor_scroll_up(editor, d);
		ret = 1;
	}

	for (;;) {
		offset = editor_offset_from_pos(editor, row,
		    editor->cursor->offset, &width_at_offset);

		diff = (int) offset - (int) editor->begin_offset;
		rbound = WIDGET_WIDTH(editor);

#ifdef WANT_LINE_NUMBERS
		assert(rbound > 100);
		rbound -= 100;
#endif

		if ((int) (diff + (int) width_at_offset) > rbound)
			editor_hscroll(editor, 1);
		else if (diff < 0 && editor->begin_offset > 0)
			editor_hscroll(editor, -1);
		else
			break;
	}

	return ret;
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

static void
editor_draw_cursor(struct editor *editor, struct cursor *cursor)
{
	draw_update(cursor->row, 0, cursor->row, 0, BUFFER_UPDATE_LINE,
	    editor);
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
		editor_draw_cursor(editor, editor->ocursor);
	editor_draw_cursor(editor, editor->cursor);
}

/*
 * Returns the string to display for a character position. We need this
 * because we want to display control characters differently, and a special
 * symbol for failed UTF-8 sequences, without modifying the original
 * string.
 */
static const char *
select_display_str(const char *p, size_t *len, int error)
{
	static const char *rep = "\xef\xbf\xbd";
	static size_t rep_len = 3;
	const char *q;
	unsigned char ch;

	if (error) {
		q = rep;
		*len = rep_len;
	} else if (*len == 1 && *p != '\t' && iscntrl((unsigned char) *p)) {
		if ((unsigned char) *p == 0x7f)
			ch = '?';
		else
			ch = *p + '@';
		q = &ch;
	} else
		q = p;

	assert(q != NULL);
	return q;
}

/*
 * Returns the pixel x-coordinate of the leftmost edge of a character
 * for the byte offset in the row.
 *
 * For example, if we have ASCII characters and character is 10px wide
 * on the screen, then this returns:
 *   -  0 for byteoffset=0;
 *   - 10 for byteoffset=1;
 *   etc.
 */
static int
editor_offset_from_pos(struct editor *editor, int row, int byteoffset,
    size_t *width_at_offset)
{
	const char *s, *p;
	size_t sz, offset, begin, len, width;
	int x, error;

	font_set(FONT_NORMAL);

	s = buffer_u8str_at(editor->buffer, row, &sz);
	if (sz == 0 || s == NULL) {
		if (width_at_offset != NULL)
			*width_at_offset = 0;
		return 0;
	}

	offset = 0;
	x = 0;
	width = 0;
	do {
		begin = offset;
		x += width;
		if (utf8_incr_col(s, sz, &offset, &error) == 0)
			break;
		assert(begin < offset);
		len = offset-begin;
		p = select_display_str(&s[begin], &len, error);
		width = font_str_width(x, p, len);
	} while(begin < byteoffset);

	if (width_at_offset != NULL)
		*width_at_offset = width;

	return x;
}

/*
 * Find from left ledge of character up to the character's width, e.g.
 * if character is 10px wide and we have ASCII characters then this
 * returns 0 for x=0 to x=9 and 1 for x=10 to x=19, etc.
 */
static int
editor_pos_from_offset(struct editor *editor, int row, int pxoffset)
{
	const char *s, *p;
	size_t sz, offset, begin, len;
	int x, error;

	font_set(FONT_NORMAL);

	s = buffer_u8str_at(editor->buffer, row, &sz);
	if (sz == 0 || s == NULL)
		return 0;

	offset = 0;
	x = 0;
	do {
		begin = offset;
		if (utf8_incr_col(s, sz, &offset, &error) == 0)
			break;
		assert(begin < offset);
		len = offset-begin;
		p = select_display_str(&s[begin], &len, error);
		x += font_str_width(x, p, len);
	} while(x <= pxoffset);

	return begin;
}

/*
 * Scroll view horizontally.
 * TODO: Optimize with XCopyArea instead of redrawing whole line.
 */
static void
editor_hscroll(struct editor *editor, int dir)
{
	switch (dir) {
	case 1:
		editor->begin_offset += WIDGET_WIDTH(editor) / 2;
		break;
	case -1:
		if (editor->begin_offset > WIDGET_WIDTH(editor) / 2)
			editor->begin_offset -= WIDGET_WIDTH(editor) / 2;
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

static int
editor_search(struct editor *editor, const char *s, size_t len,
    int dir, int want_case)
{
	size_t rows, n, offset, start, end;
	int i;
	const char *p;

	rows = buffer_rows(editor->buffer);
	if (rows == 0)
		return 0;

	start = editor->cursor->row;
	if (dir == 1)
		end = rows-1;
	else
		end = 0;

	/*
	 * Select byte offset to begin from.
	 * TODO: Implement inter-line reverse search.
	 */
	offset = editor->cursor->offset;
	p = buffer_u8str_at(editor->buffer, start, &n);
	if (dir == -1) {
		offset = 0;
		if (start > 0)
			start--;
	} else
		utf8_incr_col(p, n, &offset, NULL);

	if (dir == 1) {
		for (i = (int) start; i <= (int) end; i += dir) {
			if (buffer_match(editor->buffer, i, s, len, &offset))
				break;
			offset = 0;
		}
	} else {
		for (i = (int) start; i >= (int) end; i += dir) {
			if (buffer_match(editor->buffer, i, s, len, &offset))
				break;
			offset = 0;
		}
	}

	if (i < rows) {
		offset += len;
		buffer_set_cursor(editor->buffer, editor->cursor, i, offset);
		editor_scroll_into_view(editor, editor->cursor->row,
		    editor->cursor->offset);
		return 1;
	}
	return 0;
}

static void
editor_prompt_submit(const char *s, void *udata)
{
	struct editor *editor = udata;
	int val;

	switch (editor->prompt_action) {
	case PROMPT_ACTION_GOTO:
		val = atoi(s);
		if (val <= 0)
			return;
		val--;
		buffer_set_cursor(editor->buffer, editor->cursor, val, 0);
		editor_scroll_into_view(editor, editor->cursor->row,
		    editor->cursor->offset);
		break;
	case PROMPT_ACTION_FSEARCH:
		editor_search(editor, s, strlen(s), 1, 0);
		break;
	case PROMPT_ACTION_RSEARCH:
		editor_search(editor, s, strlen(s), -1, 0);
		break;
	default:
		assert(0);
	}

	if (editor->prompt) {
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
	editor->prefer_offset = -1;

	buffer_add_listener(cursor->buffer, draw_update, editor);

	editor_draw_cursor(editor, cursor);

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
	static char dst[4096];
	const char *p;
	size_t len;

	p = buffer_u8str_at(cursor->buffer, cursor->row, &len);
	if (p != NULL) {
		memcpy(dst, p, len);
		dst[len] = '\0';
	}

	return dst;
}

static void
editor_find_cursor_pos(struct editor *editor, int ex, int ey, int *row,
    int *offset)
{
	*row = ey / font_height();
	*row += editor->top_row;
	*offset = editor_pos_from_offset(editor, *row, ex);
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
	int row, offset;

	editor_draw_cursor(editor, editor->cursor);
#ifdef WANT_LINE_NUMBERS
	e->x -= 100;
#endif
	e->x += editor->begin_offset;
	editor_find_cursor_pos(editor, e->x, e->y, &row, &offset);
	buffer_set_cursor(editor->buffer, editor->cursor, row, offset);
	editor_scroll_into_view(editor, editor->cursor->row,
	    editor->cursor->offset);
	return 1;
}

static int
editor_mousepress(struct widget *widget, XButtonEvent *e, void *udata)
{
	struct editor *editor = udata;
	int row, col;
	const char *p;
	char *q;
	size_t sz;

	if (e->type == ButtonRelease)
		return 0;

	widget_focus(WIDGET(editor));

	switch (e->button) {
	case 1:
#ifdef WANT_LINE_NUMBERS
		e->x -= 100;
#endif
		e->x += editor->begin_offset;
		editor_find_cursor_pos(editor, e->x, e->y, &row, &col);
		buffer_set_cursor(editor->buffer, editor->cursor, row, col);
		editor_draw_cursor(editor, editor->cursor);
		return 1;
	case 3:
#ifdef WANT_LINE_NUMBERS
		e->x -= 100;
#endif
		e->x += editor->begin_offset;
		editor_find_cursor_pos(editor, e->x, e->y, &row, &col);

		p = buffer_u8str_at(editor->buffer, row, &sz);
		if (p != NULL) {
			q = malloc(sz + 1);
			if (q != NULL && editor->exec) {
				memcpy(q, p, sz);
				q[sz] = '\0';
				editor->exec(q, editor->exec_udata);
			}
			if (q != NULL)
				free(q);
		}
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
	int offset;

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

	if (sym != XK_Up && sym != XK_Down)
		vc->prefer_offset = -1;

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
			buffer_set_cursor(vc->buffer, vc->cursor,
			    vc->cursor->row, 0);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_e:
			buffer_set_cursor(vc->buffer, vc->cursor,
			    vc->cursor->row,
			    buffer_bytes_at(vc->buffer, vc->cursor->row));
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_k:
			if (buffer_bytes_at(vc->buffer, vc->cursor->row) == 0)
				buffer_remove_row(vc->buffer, vc->cursor->row);
			else
				buffer_erase_eol(vc->buffer, vc->cursor);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_b:
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -1);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_f:
			buffer_update_cursor(vc->buffer, vc->cursor, 0, 1);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_p:
			editor_draw_cursor_now(vc, 0);
			buffer_update_cursor(vc->buffer, vc->cursor, -1, 0);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_n:
			editor_draw_cursor_now(vc, 0);
			buffer_update_cursor(vc->buffer, vc->cursor, 1, 0);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_d:
			buffer_delete_char(vc->buffer, vc->cursor);
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		case XK_o:
			row = vc->cursor->row;
			col = vc->cursor->col;
			buffer_insert(vc->cursor, "\n", 1);
			vc->cursor->row = row;
			vc->cursor->col = col;
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			editor_draw_cursor(vc, vc->cursor);
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
			editor_scroll_into_view(vc, vc->cursor->row,
			    vc->cursor->offset);
			return 1;
		}
	}

	switch (sym) {
	case XK_Up:
	case XK_Down:
		if (vc->ocursor) {
			if (vc->cursor->row == vc->ocursor->row &&
			    vc->cursor->col == vc->ocursor->col)
				editor_draw_cursor(vc, vc->ocursor);
		}
		editor_draw_cursor_now(vc, 0);
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
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	case XK_Left:
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -8);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, 0, -1);
		break;
	case XK_Right:
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		if (e->state & ShiftMask)
			buffer_update_cursor(vc->buffer, vc->cursor, 0, 8);
		else
			buffer_update_cursor(vc->buffer, vc->cursor, 0, 1);
		break;
	case XK_Up:
		row = vc->cursor->row;
		if (vc->prefer_offset == -1)
			vc->prefer_offset = editor_offset_from_pos(vc, row,
			    vc->cursor->offset, NULL);
		offset = vc->prefer_offset;
		if (e->state & ShiftMask)
			row -= 8;
		else
			row -= 1;
		if (row < 0)
			row = 0;
		col = editor_pos_from_offset(vc, row, offset);
		buffer_set_cursor(vc->buffer, vc->cursor, row, col);
		break;
	case XK_Down:
		row = vc->cursor->row;
		if (vc->prefer_offset == -1)
			vc->prefer_offset = editor_offset_from_pos(vc, row,
			    vc->cursor->offset, NULL);
		offset = vc->prefer_offset;
		if (e->state & ShiftMask)
			row += 8;
		else
			row += 1;
		if (row >= buffer_rows(vc->buffer)) {
			row = buffer_rows(vc->buffer) - 1;
			if (row < 0)
				row = 0;
		}
		col = editor_pos_from_offset(vc, row, offset);
		buffer_set_cursor(vc->buffer, vc->cursor, row, col);
		break;
	case XK_Page_Up:
		editor_page_up(vc);
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	case XK_Page_Down:
		editor_page_down(vc);
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	case XK_BackSpace:
		buffer_erase(vc->buffer, vc->cursor);
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	case XK_Delete:
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	}

	switch (sym) {
	case XK_Left:
	case XK_Right:
	case XK_Up:
	case XK_Down:
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	}

	if ((n = Xutf8LookupString(WIDGET(vc)->ic, e, ch, sizeof(ch),
	    &sym, NULL)) < 0) {
		/* TODO: Handle this error case */
		n = 0;
	} else {
		buffer_insert(vc->cursor, ch, n);
		editor_scroll_into_view(vc, vc->cursor->row,
		    vc->cursor->offset);
		return 1;
	}

	return 0;
}

static void
editor_draw_cursor_now(struct editor *editor, int visible)
{
	int orig_focused;

	orig_focused = editor->focused;

	if (visible && orig_focused)
		editor->focused = 1;
	else
		editor->focused = 0;

	editor_draw(editor, editor->cursor->row, editor->cursor->row);
	editor->focused = orig_focused;
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
editor_draw_eol_cursor(struct editor *editor, size_t *x, int *sx,
    size_t row, size_t y, size_t orig_len)
{
	size_t x_add;

	if (!editor->focused)
		return;
	if (editor->cursor->row != row)
		return;
	if (editor->cursor->offset != orig_len)
		return;

	font_set_bgcolor(COLOR_TEXT_CURSOR);
	x_add = font_draw(editor->window, *x, *sx, y, " ", 1);
	*x += x_add;
	*sx += x_add;
}

static void
editor_draw_chunk(struct editor *editor, size_t *x, int *sx,
    size_t y, const char *src, size_t len, int bgcolor)
{
	const char *s;	
	char ch;
	size_t x_add;

	font_set_bgcolor(bgcolor);

	/*
	 * If we stepped on an invisible control character, draw it in
	 * a way that conveys some information because control characters
	 * other than '\t' are not interpreted here.
	 */
	if (len == 1 && *src != '\t' && iscntrl((unsigned char) *src)) {
		if ((unsigned char) *src == 0x7f)
			ch = '?';
		else
			ch = *src + '@';
		s = &ch;
	} else
		s = src;

	x_add = font_draw(editor->window, *x, *sx, y, s, len);
	*x += x_add;
	*sx += x_add;
}

/*
 * Draws and colors line, or part of line.
 *
 * Assumes valid UTF-8 e.g. data is preprocessed and checked for errors
 * before entering here.
 */
static void
editor_draw_line(struct editor *editor, size_t *x, int *sx, size_t row,
    size_t y, const char *dst, size_t len, size_t orig_offset,
    size_t orig_len)
{
	size_t j, k;
	int bgcolor, want_bgcolor, step_ctrl, error;

	if (dst == NULL || len == 0)
		return;

	j = 0;
	k = 0;
	bgcolor = editor->bgcolor;
	want_bgcolor = editor->bgcolor;
	step_ctrl = 0;
	error = 0;
	do {
		/*
		 * We shouldn't get UTF-8 errors here. If we do get,
		 * it is an input validation error.
		 */
		assert(error == 0);

		if (j >= len)
			break;

		if (editor->focused && row == editor->cursor->row &&
		    j+orig_offset == editor->cursor->offset) {
			want_bgcolor = COLOR_TEXT_CURSOR;
		} else if (dst[j] != '\t' && iscntrl((unsigned char) dst[j])) {
			want_bgcolor = COLOR_TEXT_CTRL;
			step_ctrl = 1;
		} else
			want_bgcolor = editor->bgcolor;

		/*
		 * Dump chunk so far if we have state change, ctrl code,
		 * or if the chunk grows large (so that we can limit
		 * draws that exceed window width).
		 */
		if (want_bgcolor != bgcolor || step_ctrl ||
		    j-k >= CHUNK_BREAK_LIMIT) {
			step_ctrl = 0;
			if (j-k > 0)
				editor_draw_chunk(editor, x, sx, y,
				    &dst[k], j-k, bgcolor);
			bgcolor = want_bgcolor;
			k=j;
		}
	} while (utf8_incr_col(dst, len, &j, &error) > 0 &&
	    *sx < WIDGET_WIDTH(editor));

	if (j-k > 0)
		editor_draw_chunk(editor, x, sx, y, &dst[k], j-k, bgcolor);
}

static void
editor_draw(struct editor *editor, size_t from, size_t to)
{
	int i, y;
	size_t x;
	int sx;
	size_t rows;
#ifdef WANT_LINE_NUMBERS
	char lineno[256];
#endif
	const char *dst;
	size_t len, offset, orig_offset, orig_len;
	int error;

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

#ifdef WANT_LINE_NUMBERS
		sx = 100;
#else
		sx = 0;
#endif
		x = 0;
		y = (i - editor->top_row) * font_height();
		sx -= editor->begin_offset;

		if (y >= WIDGET_HEIGHT(editor))
			continue;

		if (i < rows) {
			orig_offset = offset = 0;
			orig_len = buffer_bytes_at(editor->buffer, i);
			error = 0;
			while ((dst = buffer_u8str_break(editor->buffer, i,
			    &offset, &len, &error)) != NULL && sx <
			    WIDGET_WIDTH(editor)) {
				if (error == 1 && len > 0)
					len--;
				editor_draw_line(editor, &x, &sx, i, y,
				    dst, len, orig_offset, orig_len);
				if (error == 1) {
					editor_draw_line(editor, &x, &sx, i,
					    y, "\xef\xbf\xbd", 3,
					    orig_offset+len,
					    orig_len);
				}
				orig_offset = offset;
			}
			editor_draw_eol_cursor(editor, &x, &sx, i, y,
			    orig_len);
		}

		if (WIDGET_WIDTH(editor)-sx > 0) {
			font_set_bgcolor(editor->bgcolor);
			font_clear(editor->window, sx, y,
			    WIDGET_WIDTH(editor) - sx);
		}
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
		if (i < rows &&
		    i % (WIDGET_HEIGHT(editor) / font_height()) == 0)
			snprintf(lineno, sizeof(lineno), "%d->", i + 1);
		else if (i < rows)
			snprintf(lineno, sizeof(lineno), "%d", i + 1);
		else
			snprintf(lineno, sizeof(lineno), "~");
		x += font_draw(editor->window, x, x, y, lineno,
		    strlen(lineno));
		if (x < 100)
			font_clear(editor->window, x, y, 100 - x);
	}
#endif

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
	int lines;

	lines = (int) ceil(height / (double) font_height());
	from = editor->top_row + (y / font_height());
	assert(lines > 0);
	to = from + (lines-1);
	editor_draw(editor, from, to);
}
