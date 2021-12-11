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

struct row {
	wchar_t *cols;
	size_t n_cols;
	size_t max_cols;
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
static int insert_row(struct buffer *, int);
static int insert_char(struct cursor *, wchar_t);

#if 0
static void dump_buffer(struct buffer *buffer);
#endif

static void broadcast_update(struct buffer *, int, int, int, int,
	BufferUpdate);

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

	if (insert_row(buffer, 0) == -1) {
		free(buffer);
		return NULL;
	}
	return buffer;
}

void
buffer_free(struct buffer *buffer)
{
	size_t i;

	assert(buffer != NULL);

	if (buffer->listeners != NULL) {
		free(buffer->listeners);
		buffer->listeners = NULL;
		buffer->n_listeners = buffer->max_listeners = 0;
	}

	for (i = 0; i < buffer->n_rows; i++)
		if (buffer->rows[i].cols != NULL)
			free(buffer->rows[i].cols);
	free(buffer->rows);

	buffer->n_rows = buffer->max_rows = 0;
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
insert_row(struct buffer *buffer, int row)
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
	cursor->row += row_add;
	cursor->col += col_add;
	buffer_restrain_cursor(buffer, cursor);
}

static void
buffer_restrain_cursor(struct buffer *buffer, struct cursor *cursor)
{
	assert(buffer->n_rows >= 1);

	if (cursor->row < 0)
		cursor->row = 0;
	if (cursor->col < 0)
		cursor->col = 0;

	if (cursor->row >= buffer->n_rows)
		cursor->row = buffer->n_rows-1;

	if (cursor->col >= buffer->rows[cursor->row].n_cols)
		cursor->col = buffer->rows[cursor->row].n_cols;
}

/*
 * Returns -1 if error.
 */
static int
insert_char(struct cursor *cursor, wchar_t ch)
{
	struct buffer *buffer;
	struct row *rowptr;
	size_t row, col;

	assert(cursor != NULL);

	buffer = CURSOR_BUFFER(cursor);	
	assert(buffer != NULL);

	buffer_restrain_cursor(buffer, cursor);

	row = CURSOR_ROW(cursor);
	rowptr = &buffer->rows[row];
	assert(rowptr != NULL);

	col = CURSOR_COL(cursor);
	if (col == rowptr->max_cols)
		if (grow_array((void **) &rowptr->cols,
		    sizeof(*rowptr->cols), &rowptr->max_cols) == -1)
			return -1;

	if (col != rowptr->n_cols) {
		memmove(&rowptr->cols[col+1], &rowptr->cols[col],
		    (rowptr->n_cols - col) * sizeof(*rowptr->cols));
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
buffer_erase(struct buffer *buffer, struct cursor *cursor)
{
	buffer_restrain_cursor(buffer, cursor);

	buffer->rows[cursor->row].n_cols--;
	if (!(buffer->rows[cursor->row].n_cols == 0 ||
	    buffer->rows[cursor->row].n_cols-1 == cursor->col+1)) {
		memmove(
		    &buffer->rows[cursor->row].cols[cursor->col],
		    &buffer->rows[cursor->row].cols[cursor->col+1],
		    (buffer->rows[cursor->row].n_cols - cursor->col) *
		    sizeof(wchar_t));
	}
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

	while (len > 0) {
		n = mbtowc(&wc, data, len);
		/* TODO: Handle n==0 */
		if (n <= 0 || wc == '\r') {
			n = 1;
		} else if (wc == '\n') {
			CURSOR_ROW(cursor)++;
			if (insert_row(cursor->buffer, CURSOR_ROW(cursor))
			    == -1)
				return -1;
			CURSOR_COL(cursor) = 0;
			row = CURSOR_ROW(cursor);
			col = CURSOR_COL(cursor);
		} else {
			if (insert_char(cursor, wc) == -1)
				return -1;

			row = CURSOR_ROW(cursor);
			col = CURSOR_COL(cursor)++;
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
