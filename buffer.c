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

#include "buffer.h"
#include "util.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <limits.h>

struct row {
	wchar_t *cols;
	size_t n_cols;
	size_t max_cols;
	int uflags;
};

struct buffer_listener {
	BLCallback callback;
	void *udata;
};

struct buffer {
	struct row *rows;
	size_t n_rows;
	size_t max_rows;

	struct buffer_listener *listeners;
	size_t n_listeners;
	size_t max_listeners;
};

static void buffer_restrain_cursor(struct buffer *, struct cursor *);
static int buffer_insert_row(struct buffer *, int);
static int buffer_insert_char(struct buffer *, int, int, wchar_t);

#if 0
static void dump_buffer(struct buffer *buffer);
#endif

static void broadcast_update(struct buffer *, int, int, int, int,
	BufferUpdate);


int
buffer_row_uflags(struct buffer *buffer, int row)
{
	if (row >= buffer->n_rows)
		return 0;

	return buffer->rows[row].uflags;
}

void
buffer_set_row_uflags(struct buffer *buffer, int row, int uflags)
{
	if (row >= buffer->n_rows)
		return;

	buffer->rows[row].uflags = uflags;
}

size_t
buffer_cols(
	struct buffer *buffer, size_t row)
{
	if (row >= buffer->n_rows)
		return 0;

	return buffer->rows[row].n_cols;
}

size_t
buffer_rows(
	struct buffer *buffer)
{
	return buffer->n_rows;
}

wchar_t
buffer_at(
	struct buffer *buffer, size_t row, size_t col)
{
	if (row >= buffer->n_rows || col >= buffer->rows[row].n_cols)
		return '\0';

	return buffer->rows[row].cols[col];
}

void
buffer_clear_row(
	struct buffer *buffer,
	int row)
{
	if (row >= buffer->n_rows)
		return;

	buffer->rows[row].n_cols = 0;
	buffer->rows[row].uflags = 0;

	broadcast_update(buffer, row, 0, row, 0, BUFFER_UPDATE_LINE);
}

/*
 * Does not NUL-terminate the string because we want to edit contents
 * that may contain NULs, but we leave space in 'dst' for a NUL to be
 * added by the caller (we check we have MB_CUR_MAX + 1 space...)
 */
size_t
buffer_u8str_at(
	struct buffer *buffer,
	size_t row,
	size_t begin_col,
	ssize_t end_col,
	char *dst,
	size_t len)
{
	size_t i, tmp, outlen;

	if (row >= buffer->n_rows)
		return 0;
	if (begin_col >= buffer->rows[row].n_cols)
		return 0;

	outlen = 0;
	if (end_col == -1 || end_col > buffer->rows[row].n_cols)
		end_col = buffer->rows[row].n_cols;
	else
		end_col++;

	for (i = begin_col; i < end_col &&
	    len >= MB_CUR_MAX + 1; i++) {
		if ((tmp = wctomb(dst, buffer->rows[row].cols[i])) == -1)
			continue;
		dst++;
		len -= tmp;
		outlen += tmp;
	}

	return outlen;
}

struct buffer *
buffer_create()
{
	struct buffer *buffer;

	buffer = calloc(1, sizeof(struct buffer));
	if (buffer == NULL)
		return NULL;

	return buffer;
}

void
buffer_clear(struct buffer *buffer)
{
	while (buffer->n_rows)
		buffer_remove_row(buffer, buffer->n_rows-1);

	if (buffer->rows != NULL) {
		free(buffer->rows);
		buffer->rows = NULL;
	}
	buffer->max_rows = 0;
}

void
buffer_free(struct buffer *buffer)
{
	assert(buffer != NULL);

	if (buffer->listeners != NULL) {
		free(buffer->listeners);
		buffer->listeners = NULL;
		buffer->n_listeners = buffer->max_listeners = 0;
	}

	buffer_clear(buffer);
	free(buffer);
}

void
buffer_remove_listener(struct buffer *buffer, BLCallback callback)
{
	size_t i;

	for (i = 0; i < buffer->n_listeners; i++)
		if (buffer->listeners[i].callback == callback)
			break;
	if (i == buffer->n_listeners) {
		warnx("did not find buffer listener to remove");
		return;
	}

	if (i+1 < buffer->n_listeners)
		memmove(&buffer->listeners[i], &buffer->listeners[i+1],
		    (buffer->n_listeners - i) *
		    sizeof(struct buffer_listener));
	buffer->n_listeners--;
}

/*
 * Returns -1 if there was error.
 */
int
buffer_add_listener(struct buffer *buffer, BLCallback callback,
    void *udata)
{
	assert(buffer != NULL);

	if (buffer->max_listeners == buffer->n_listeners)
		if (grow_array((void **) &buffer->listeners,
		    sizeof(*buffer->listeners), &buffer->max_listeners) == -1)
			return -1;

	buffer->listeners[buffer->n_listeners++] =
	    (struct buffer_listener) { callback, udata };

	return 0;
}

struct cursor *
buffer_cursor_create(struct buffer *buffer)
{
	struct cursor *cursor;

	assert(buffer != NULL);

	if ((cursor = calloc(1, sizeof(struct cursor))) == NULL)
		return NULL;

	/*
	 * TODO: Add buffer listener for updating the cursors...
	 */

	cursor->buffer = buffer;
	return cursor;
}

void
buffer_cursor_free(struct cursor *cursor)
{
	/* TODO: unregister_buffer_listener */
	free(cursor);	
}

/*
 * Returns -1 if error.
 */
static int
buffer_insert_row(struct buffer *buffer, int row)
{
	assert(buffer != NULL);

	if (buffer->n_rows == buffer->max_rows)
		if (grow_array((void **) &buffer->rows,
		    sizeof(*buffer->rows), &buffer->max_rows) == -1)
			return -1;

	memmove(&buffer->rows[row+1], &buffer->rows[row],
	    (buffer->n_rows - row) * sizeof(*buffer->rows));

	memset(&buffer->rows[row], '\0', sizeof(*buffer->rows));

	buffer->n_rows++;
	return 0;
}

void
buffer_update_cursor(
	struct buffer *buffer,
	struct cursor *cursor,
	int row_add,
	int col_add)
{
	int old_row, old_col;

	old_row = cursor->row;
	old_col = cursor->col;

	cursor->row += row_add;
	cursor->col += col_add;

	buffer_restrain_cursor(buffer, cursor);
	broadcast_update(buffer, old_row, old_col, old_row, old_col,
	    BUFFER_UPDATE_LINE);
	broadcast_update(buffer, cursor->row, cursor->col, cursor->row,
	    cursor->col, BUFFER_UPDATE_LINE);
}

static void
buffer_restrain_cursor(struct buffer *buffer, struct cursor *cursor)
{
	if (cursor->row < 0)
		cursor->row = 0;
	if (cursor->col < 0)
		cursor->col = 0;

	if (buffer->n_rows == 0)
		cursor->row = 0;
	else if (cursor->row >= buffer->n_rows)
		cursor->row = buffer->n_rows-1;

	if (buffer->n_rows == 0)
		cursor->col = 0;
	else if (cursor->col >= buffer->rows[cursor->row].n_cols)
		cursor->col = buffer->rows[cursor->row].n_cols;
}

/*
 * Returns -1 if error.
 */
static int
buffer_insert_char(struct buffer *buffer, int row, int col, wchar_t ch)
{
	struct row *rowptr;
	void *dst, *src;
	size_t len;

	if (buffer->n_rows == 0)
		if (buffer_insert_row(buffer, 0) == -1)
			return -1;

	assert(buffer->n_rows > 0);
	row = MIN(row, buffer->n_rows-1);
	rowptr = &buffer->rows[row];
	assert(rowptr != NULL);

	if (rowptr->n_cols == rowptr->max_cols)
		if (grow_array((void **) &rowptr->cols,
		    sizeof(*rowptr->cols), &rowptr->max_cols) == -1)
			return -1;

	col = MIN(col, rowptr->n_cols);
	if (col < rowptr->n_cols) {
		dst = &rowptr->cols[col+1];
		src = &rowptr->cols[col];
		len = (rowptr->n_cols-col) * sizeof(*rowptr->cols);
		memmove(dst, src, len);
	}

	rowptr->n_cols++;
	rowptr->cols[col] = ch;
	return 0;
}

static void
broadcast_update(struct buffer *buffer,
	int from_row, int from_col, int to_row, int to_col,
	BufferUpdate type)
{
	size_t i;

	for (i = 0; i < buffer->n_listeners; i++)
		buffer->listeners[i].callback(from_row, from_col, to_row,
		    to_col, type, buffer->listeners[i].udata);
}

void
buffer_remove_row(struct buffer *buffer, int row)
{
	void *dst, *src;
	size_t len;
	int from, to;

	if (buffer->n_rows == 0)
		return;

	assert(row < buffer->n_rows);

	if (buffer->rows[row].cols != NULL)
		free(buffer->rows[row].cols);

	if (row+1 < buffer->n_rows) {
		dst = &buffer->rows[row];
		src = &buffer->rows[row+1];
		len = (buffer->n_rows-row-1) * sizeof(struct row);
		memmove(dst, src, len);
	}
	buffer->n_rows--;

	from = row > 0 ? row-1 : 0;
	to = buffer->n_rows > 0 ? buffer->n_rows-1 : 0;
	broadcast_update(buffer, from, 0, to, 0, BUFFER_UPDATE_LINE);
}

void
buffer_erase_eol(struct buffer *buffer, struct cursor *cursor)
{
	buffer_restrain_cursor(buffer, cursor);

	if (buffer->n_rows == 0)
		return;

	buffer->rows[cursor->row].n_cols -=
	    (buffer->rows[cursor->row].n_cols - cursor->col);

	buffer_restrain_cursor(buffer, cursor);

	broadcast_update(cursor->buffer, cursor->row, cursor->col,
	    cursor->row, INT_MAX, BUFFER_UPDATE_LINE);
}

void
buffer_delete_char(struct buffer *buffer, struct cursor *cursor)
{
	void *dst, *src;
	size_t len, i;

	buffer_restrain_cursor(buffer, cursor);

	if (cursor->col == buffer->rows[cursor->row].n_cols) {
		/*
		 * Join head of this line to the line below this.
		 */
		if (cursor->row+1 < buffer->n_rows)
			for (i = 0; i < buffer->rows[cursor->row].n_cols; i++)
				buffer_insert_char(buffer,
				    cursor->row+1, INT_MAX,
				    buffer->rows[cursor->row].cols[i]);

		buffer_remove_row(buffer, cursor->row);
		buffer_restrain_cursor(buffer, cursor);
		return;
	}

	if (cursor->col+1 < buffer->rows[cursor->row].n_cols) {
		dst = &buffer->rows[cursor->row].cols[cursor->col];
		src = &buffer->rows[cursor->row].cols[cursor->col+1];
		len = (buffer->rows[cursor->row].n_cols - cursor->col) *
		    sizeof(wchar_t);
		memmove(dst, src, len);
	}

	if (buffer->rows[cursor->row].n_cols > 0)
		buffer->rows[cursor->row].n_cols--;

	buffer_restrain_cursor(buffer, cursor);

	broadcast_update(cursor->buffer, cursor->row, cursor->col,
	    cursor->row, cursor->col, BUFFER_UPDATE_LINE);	
}

void
buffer_erase(struct buffer *buffer, struct cursor *cursor)
{
	int i;

	buffer_restrain_cursor(buffer, cursor);

	if (cursor->col == 0) {
		if (cursor->row == 0)
			return;

		/*
		 * Join tail of this line to the line above this.
		 */
		for (i = 0; i < buffer->rows[cursor->row].n_cols; i++)
			buffer_insert_char(buffer,
			    cursor->row-1, INT_MAX,
			    buffer->rows[cursor->row].cols[i]);

		buffer_remove_row(buffer, cursor->row);
		cursor->row--;
		cursor->col = buffer->rows[cursor->row].n_cols - i;
		buffer_restrain_cursor(buffer, cursor);
		return;
	}

	if (buffer->rows[cursor->row].n_cols > 0) {
		buffer->rows[cursor->row].n_cols--;
		cursor->col--;
	}

	if (!(buffer->rows[cursor->row].n_cols == 0 ||
	    buffer->rows[cursor->row].n_cols-1 == cursor->col+1)) {
		memmove(
		    &buffer->rows[cursor->row].cols[cursor->col],
		    &buffer->rows[cursor->row].cols[cursor->col+1],
		    (buffer->rows[cursor->row].n_cols - cursor->col) *
		    sizeof(wchar_t));
	}

	buffer_restrain_cursor(buffer, cursor);

	broadcast_update(cursor->buffer, cursor->row, cursor->col,
	    cursor->row, cursor->col, BUFFER_UPDATE_LINE);
}

/*
 * Returns -1 if error.
 */
int
buffer_insert(struct cursor *cursor, const char *data, size_t len)
{
	int row, col;
	int n;
	wchar_t wc;
	int from_row, from_col, to_row, to_col;

	row = from_row = CURSOR_ROW(cursor);
	col = from_col = CURSOR_COL(cursor);

	buffer_restrain_cursor(cursor->buffer, cursor);

	while (len > 0) {
		n = mbtowc(&wc, data, len);
		/* TODO: Handle n==0 */
		if (n <= 0 || wc == '\r')
			n = 1;
		else if (wc == '\n') {
			CURSOR_ROW(cursor)++;
			if (buffer_insert_row(cursor->buffer,
			    CURSOR_ROW(cursor)) == -1)
				return -1;
			CURSOR_COL(cursor) = 0;
			row = CURSOR_ROW(cursor);
			col = CURSOR_COL(cursor);
		} else {
			if (buffer_insert_char(cursor->buffer, row, col,
			    wc) == -1)
				return -1;

			col = ++CURSOR_COL(cursor);
		}
		len -= n;
		data += n;
	}

	to_row = row;
	to_col = col;

	broadcast_update(cursor->buffer,
	    from_row, from_col, to_row, to_col, BUFFER_UPDATE_LINE);
	return 0;
}

#if 0
static void
dump_buffer(struct buffer *buffer)
{
	size_t row, col;
	struct row *rowptr;

	assert(buffer != NULL);

	for (row = 0; row < buffer->n_rows; row++) {
		rowptr = &buffer->rows[row];
		for (col = 0; col < rowptr->n_cols; col++)
			putchar(rowptr->cols[col]);
		putchar('\n');
	}
}
#endif
