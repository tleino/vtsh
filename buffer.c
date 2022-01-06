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
#include "utf8.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <err.h>
#include <limits.h>
#include <ctype.h>

struct row {
	char *bytes;
	size_t bytes_used;
	size_t bytes_size;
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

	int has_mark;
	struct cursor mark;
};

static int	 buffer_insert_row(struct buffer *, int);
static int	 buffer_insert_char(struct buffer *, size_t, size_t *,
		    const char *, size_t);
static char	*row_at(struct row *, size_t *, size_t *);
static void	 buffer_erase_eol_at(struct buffer *, size_t, size_t);
static void	 broadcast_update(struct buffer *, int, int, int, int,
		    BufferUpdate);
static void	 buffer_update(struct buffer *, int, int);

#if 0
static void	 dump_buffer(struct buffer *buffer);
#endif

void
buffer_set_mark(struct buffer *buffer, size_t row, size_t offset)
{
	struct row *rp;

	assert(row < buffer->n_rows);
	if (row >= buffer->n_rows)
		return;

	rp = &buffer->rows[row];
	assert(offset < rp->bytes_used);
	if (offset >= rp->bytes_used)
		return;

	buffer_clear_mark(buffer, row);
	buffer->has_mark = 1;
	buffer->mark.row = row;
	buffer->mark.offset = offset;
}

int
buffer_has_mark(struct buffer *buffer)
{
	return buffer->has_mark;
}

int
buffer_is_marked(struct buffer *buffer, size_t row, size_t offset,
    size_t dot_row, size_t dot_offset)
{
	if (buffer->has_mark == 0)
		return 0;
	else if (buffer->mark.row > row)
		return 0;
	else if (buffer->mark.row == row && dot_row > row &&
	    offset >= buffer->mark.offset)
		return 1;
	else if (buffer->mark.row < row && dot_row > row)
		return 1;
	else if (buffer->mark.row < row && dot_row == row &&
	    offset < dot_offset)
		return 1;
	else if (buffer->mark.row == row && dot_row == row &&
	    offset >= buffer->mark.offset && offset < dot_offset)
		return 1;
	else
		return 0;
}

void
buffer_clear_mark(struct buffer *buffer, size_t current_row)
{
	if (!buffer->has_mark)
		return;

	buffer->has_mark = 0;
	buffer_update(buffer, buffer->mark.row, current_row);
	memset(&buffer->mark, '\0', sizeof(struct cursor));
}

int
buffer_row_uflags(struct buffer *buffer, int row)
{
	if (row >= buffer->n_rows)
		return 0;

	return buffer->rows[row].uflags;
}

/*
 * Select word at position, or whole line if we're pointing at EOL.
 * Offset is modified to point at the end of the word.
 */
const char *
buffer_word_at(struct buffer *buffer, size_t row, size_t *offset,
    size_t *sz_out)
{
	size_t begin, end, orig_offset;
	const char *s;
	size_t len;
	struct row *rowptr;

	if (row >= buffer->n_rows)
		return NULL;

	rowptr = &buffer->rows[row];
	if (*offset >= rowptr->bytes_used) {
		*offset = 0;
		*sz_out = rowptr->bytes_used;
		return rowptr->bytes;
	}

	len = rowptr->bytes_used;
	s = rowptr->bytes;
	orig_offset = *offset;
	while (isspace(rowptr->bytes[*offset]) &&
	    utf8_decr_col(s, len, offset) > 0)
		;

	if (isspace(rowptr->bytes[*offset])) {
		*offset = orig_offset;
		*sz_out = 0;
		return NULL;
	}

	orig_offset = *offset;

	while (!isspace(rowptr->bytes[*offset]) &&
	    utf8_decr_col(s, len, offset) > 0)
		;

	if (isspace(rowptr->bytes[*offset]))
		begin = *offset + 1;
	else
		begin = *offset;
	*offset = orig_offset;

	while (!isspace(rowptr->bytes[*offset]) &&
	    utf8_incr_col(s, len, offset, NULL) > 0)
		;

	end = *offset;
	if (begin > end) {
		*sz_out = 0;
		*offset = orig_offset;
		return NULL;
	}
	*sz_out = end - begin;
	if (*sz_out == 0) {
		*offset = orig_offset;
		return NULL;
	}

	return &s[begin];
}

/*
 * Quick and dirty strcasestr() replacement that works with non-NULL
 * terminated strings. Returns 1 if found.
 */
int
buffer_match(struct buffer *buffer, size_t row, const char *needle,
    size_t needle_len, size_t *offset)
{
	const char *haystack;
	size_t haystack_len, len, target_offset, prev, begin, orig_len;
	const char *p, *q, *tmp;

	if (row >= buffer->n_rows)
		return 0;

	if (needle == NULL || needle_len == 0)
		return 0;

	assert(offset != NULL);
	begin = *offset;
	haystack = &buffer->rows[row].bytes[begin];
	if (haystack == NULL)
		return 0;

	haystack_len = buffer->rows[row].bytes_used - begin;
	orig_len = haystack_len;
	p = haystack;
	q = needle;
	while (haystack_len > 0) {
		while(*p != *q && haystack_len > 0) {
			p++;
			haystack_len--;
		}
		if (haystack_len == 0)
			break;

		len = needle_len;
		tmp = p;
		p++;
		q++;
		len--;
		while(*q++ == *p++ && len > 0)
			len--;

		q = needle;
		if (len == 0) {
			p = tmp;
			break;	
		} else if (haystack_len > 0) {
			p = ++tmp;
			haystack_len--;
		}
	}

	/*
	 * Return early if we did not find.
	 */
	if (haystack_len == 0)
		return 0;

	/*
	 * We found it. Now, align to valid UTF-8 boundary.
	 */
	target_offset = (size_t) (p - haystack);
	*offset = 0;
	prev = 0;
	size_t j = 0;
	while (*offset < target_offset &&
	    (j = utf8_incr_col(haystack, orig_len, offset, NULL)) > 0)
		prev = *offset;

	if (*offset > target_offset)
		*offset = prev;

	*offset += begin;
	return 1;
}

void
buffer_set_row_uflags(struct buffer *buffer, int row, int uflags)
{
	if (row >= buffer->n_rows)
		return;

	buffer->rows[row].uflags = uflags;
}

size_t
buffer_rows(
	struct buffer *buffer)
{
	if (buffer->n_rows == 0)
		if (buffer_insert_row(buffer, 0) == -1)
			return 0;

	return buffer->n_rows;
}

void
buffer_clear_row(
	struct buffer *buffer,
	int row)
{
	if (row >= buffer->n_rows)
		return;

	buffer->rows[row].bytes_used = 0;
	buffer->rows[row].bytes_size = 0;
	if (buffer->rows[row].bytes != NULL) {
		free(buffer->rows[row].bytes);
		buffer->rows[row].bytes = NULL;
	}
	buffer->rows[row].uflags = 0;

	/* TODO: Update cursors properly */

	broadcast_update(buffer, row, 0, row, 0, BUFFER_UPDATE_LINE);
}

/*
 * Does not NUL-terminate the string because we want to edit contents
 * that may contain NULs.
 */
const char *
buffer_u8str_at(struct buffer *buffer, size_t row, size_t *sz_out)
{
	if (buffer->n_rows == 0)
		if (buffer_insert_row(buffer, 0) == -1)
			return NULL;
	
	if (row >= buffer->n_rows)
		return 0;
	*sz_out = buffer->rows[row].bytes_used;
	return buffer->rows[row].bytes;
}

/*
 * Iterate over string according to UTF-8 rules.
 *
 * Returns NULL if trying to advance over EOL i.e. that's the signal
 * we're done.
 *
 * Otherwise returns buffer containing string parsed so far. If error
 * is set then the last character in the returned string should be replaced
 * with a replacement character U+FFFD.
 */
const char *
buffer_u8str_break(struct buffer *buffer, size_t row, size_t *offset,
    size_t *sz_out, int *error)
{
	size_t begin;
	struct row *rowptr;

	*sz_out = 0;

	if (buffer->n_rows == 0)
		if (buffer_insert_row(buffer, 0) == -1)
			return NULL;
	
	if (row >= buffer->n_rows)
		return NULL;
	rowptr = &buffer->rows[row];

	if (*offset == rowptr->bytes_used)
		return NULL;

	begin = *offset;
	while(utf8_incr_col(rowptr->bytes, rowptr->bytes_used,
	    offset, error) > 0 && *error == 0)
		;

	if (*offset == begin)
		return NULL;

	assert(begin < *offset);
	*sz_out = *offset - begin;
	return &rowptr->bytes[begin];
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

	buffer_clear_mark(buffer, 0);
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

	if (buffer->n_rows == buffer->max_rows) {
		if (grow_array((void **) &buffer->rows,
		    sizeof(*buffer->rows), &buffer->max_rows) == -1)
			return -1;
	}

	if (row < buffer->n_rows) {
		memmove(&buffer->rows[row+1], &buffer->rows[row],
		    (buffer->n_rows - row) * sizeof(*buffer->rows));
	}

	if (buffer->n_rows - row > 0)
		broadcast_update(buffer, row, 0, buffer->n_rows-1, 0,
		    BUFFER_UPDATE_LINE);

	memset(&buffer->rows[row], '\0', sizeof(*buffer->rows));

	buffer->n_rows++;
	return 0;
}

static void
buffer_update(struct buffer *buffer, int from, int to)
{
	int i;

	if (from < to)
		for (i = to; i >= from; i--)
			broadcast_update(buffer, i, 0, i, 0,
			    BUFFER_UPDATE_LINE);
	else
		for (i = from; i >= to; i--)
			broadcast_update(buffer, i, 0, i, 0,
			    BUFFER_UPDATE_LINE);
}

void
buffer_set_cursor(struct buffer *buffer, struct cursor *cursor, int row,
    int offset)
{
	int old_row, old_col;

	if (row < 0 || buffer->n_rows == 0)
		row = 0;
	else if (row >= buffer->n_rows)
		row = buffer->n_rows-1;

	if (offset > buffer->rows[row].bytes_used)
		offset = buffer->rows[row].bytes_used;
	else if (offset < 0)
		offset = 0;

	old_row = cursor->row;
	old_col = cursor->col;
	cursor->row = row;
	cursor->offset = offset;

	buffer_update(buffer, old_row, cursor->row);
}

/*
 * Decrease offset in the UTF-8 string by _one_ cursor position.
 */
void
row_decr_col(struct row *rowptr, size_t *offset)
{
	utf8_decr_col(rowptr->bytes, rowptr->bytes_used, offset);
}

/*
 * Increase offset in the UTF-8 string by _one_ cursor position.
 */
void
row_incr_col(struct row *rowptr, size_t *offset)
{
	utf8_incr_col(rowptr->bytes, rowptr->bytes_used, offset, NULL);
}

void
buffer_update_cursor(struct buffer *buffer, struct cursor *cursor,
    int row_add, int col_add)
{
	int old_row, old_col;
	struct row *rowptr;

	old_row = cursor->row;
	old_col = cursor->col;

	if (row_add < 0) {
		row_add *= -1;
		while (row_add--)
			if (cursor->row > 0)
				cursor->row--;
	} else if (row_add > 0) {
		while (row_add--)
			if (cursor->row+1 < buffer->n_rows)
				cursor->row++;
	}

	rowptr = &buffer->rows[cursor->row];
	if (col_add < 0) {
		col_add *= -1;
		while (col_add--) {
			if (cursor->offset > 0)
				row_decr_col(rowptr, &cursor->offset);
			else if (cursor->row > 0) {
				cursor->row--;
				rowptr = &buffer->rows[cursor->row];
				cursor->offset = rowptr->bytes_used;
			}
		}
	} else if (col_add > 0) {
		while (col_add--) {
			if (cursor->offset < rowptr->bytes_used)
				row_incr_col(rowptr, &cursor->offset);
			else if (cursor->row+1 < buffer->n_rows) {
				cursor->row++;
				rowptr = &buffer->rows[cursor->row];
				cursor->offset = 0;
			}
		}
	}

	buffer_update(buffer, old_row, cursor->row);
}

static int
buffer_make_space(struct row *rowptr, size_t offset, size_t sz)
{
	void *dst, *src;
	size_t len;

	while (sz > 0 && sz--) {
		if (rowptr->bytes_used == rowptr->bytes_size)
			if (grow_array((void **) &rowptr->bytes,
			    sizeof(*rowptr->bytes), &rowptr->bytes_size)
			    == -1)
				return -1;

		rowptr->bytes_used++;

		if (offset+1 < rowptr->bytes_used) {
			dst = &rowptr->bytes[offset+1];
			src = &rowptr->bytes[offset];
			len = (rowptr->bytes_used-offset) *
			    sizeof(*rowptr->bytes);
			memmove(dst, src, len);
		}

		offset++;
	}
	return 0;
}

static int
buffer_shrink_space(struct row *rowptr, size_t offset, size_t sz)
{
	void *dst, *src;
	size_t len;

	while (sz > 0 && sz-- && rowptr->bytes_used > 0) {
		if (offset+1 < rowptr->bytes_used) {
			dst = &rowptr->bytes[offset];
			src = &rowptr->bytes[offset+1];
			len = (rowptr->bytes_used-offset-1) *
			    sizeof(*rowptr->bytes);
			memmove(dst, src, len);
		}
		rowptr->bytes_used--;
	}
	if (rowptr->bytes_used == 0 && rowptr->bytes != NULL) {
		free(rowptr->bytes);
		rowptr->bytes_size = 0;
		rowptr->bytes = NULL;
	}
	return 0;
}

/*
 * Returns -1 if error, otherwise returns len used.
 */
static int
buffer_insert_char(struct buffer *buffer, size_t row, size_t *offset,
    const char *s, size_t len)
{
	struct row *rowptr;
	size_t o_offset;

	if (buffer->n_rows == 0)
		if (buffer_insert_row(buffer, 0) == -1)
			return -1;

	assert(buffer->n_rows > 0);
	row = MIN(row, buffer->n_rows-1);
	rowptr = &buffer->rows[row];
	assert(rowptr != NULL);

	if (buffer_make_space(rowptr, *offset, len) == -1)
		return -1;

	o_offset = *offset;
	while (len > 0 && len--) {
		rowptr->bytes[*offset] = *s++;
		(*offset)++;
	}

	if (buffer->has_mark && buffer->mark.row == row)
		if (o_offset < buffer->mark.offset)
			buffer_update_cursor(buffer, &buffer->mark, 0, 1);

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

	if (buffer->n_rows == 0 || row < 0)
		return;

	if (buffer->rows[row].bytes != NULL)
		free(buffer->rows[row].bytes);

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

static void
buffer_erase_eol_at(struct buffer *buffer, size_t row, size_t offset)
{
	struct row *rowptr;
	size_t len;

	if (buffer->n_rows == 0)
		return;

	/* TODO: Use delete_char or handle mark updates here also */

	rowptr = &buffer->rows[row];
	assert(rowptr->bytes_used >= offset);
	len = rowptr->bytes_used - offset;

	rowptr->bytes_used -= len;

	broadcast_update(buffer, row, 0, row, 0, BUFFER_UPDATE_LINE);
}

void
buffer_erase_eol(struct buffer *buffer, struct cursor *cursor)
{
	buffer_erase_eol_at(buffer, cursor->row, cursor->offset);
}

void
buffer_delete_char(struct buffer *buffer, struct cursor *cursor)
{
	size_t eol, offset, sz, m_offset, had_mark;
	const char *p;
	struct row *rowptr;

	if (buffer->n_rows == 0)
		return;

	if (cursor->offset == buffer->rows[cursor->row].bytes_used &&
	    buffer->n_rows > 1) {
		/*
		 * Join head of the line below.
		 */
		if (cursor->row+1 < buffer->n_rows) {
			eol = buffer->rows[cursor->row].bytes_used;
			if (buffer->has_mark &&
			    buffer->mark.row == cursor->row+1) {
				had_mark = 1;
				m_offset = buffer->mark.offset + eol;
				buffer_set_cursor(buffer, &buffer->mark,
				    cursor->row, 0);
			}
			offset = 0;
			while ((p = row_at(&buffer->rows[cursor->row+1],
			    &offset, &sz)) != NULL)
				if (buffer_insert_char(buffer, cursor->row,
				    &eol, p, sz) == -1)
					return;
			if (had_mark)
				buffer_set_cursor(buffer, &buffer->mark,
				    cursor->row, m_offset);
		}

		if (cursor->row+1 < buffer->n_rows)
			buffer_remove_row(buffer, cursor->row+1);
		return;
	}

	if (cursor->row >= buffer->n_rows)
		return;

	offset = cursor->offset;
	if (buffer->has_mark && buffer->mark.row == cursor->row) {
		if (offset < buffer->mark.offset) {
			buffer_update_cursor(buffer, &buffer->mark, 0, -1);
		} else if (offset == buffer->mark.offset) {
			buffer_clear_mark(buffer, cursor->row);
		}
	}

	rowptr = &buffer->rows[cursor->row];
	p = row_at(rowptr, &offset, &sz);
	if (p != NULL)
		if (buffer_shrink_space(rowptr, cursor->offset, sz) == -1)
			return;

	broadcast_update(cursor->buffer, cursor->row, cursor->col,
	    cursor->row, cursor->col, BUFFER_UPDATE_LINE);	
}

void
buffer_erase(struct buffer *buffer, struct cursor *cursor)
{
	if (cursor->row == 0 && cursor->offset == 0)
		return;
	buffer_update_cursor(buffer, cursor, 0, -1);
	buffer_delete_char(buffer, cursor);
	return;
}

/*
 * Returns -1 if error.
 */
int
buffer_insert(struct cursor *cursor, const char *s, size_t len)
{
	int row, col;
	int from_row, from_col;
	struct buffer *buffer = cursor->buffer;
	unsigned char ch;
	char *p;
	size_t offset, offset2;
	size_t sz;

	row = from_row = CURSOR_ROW(cursor);
	col = from_col = CURSOR_COL(cursor);

	if (buffer->n_rows == 0)
		if (buffer_insert_row(buffer, 0) == -1)
			return -1;

	offset = cursor->offset;

	while (len > 0 && len--) {
		ch = (unsigned char) *s;
		if (ch == '\n') {
			cursor->offset = offset;

			if (buffer_insert_row(buffer, cursor->row+1) == -1)
				return -1;
			cursor->row++;

			offset2 = 0;
			while ((p = row_at(&buffer->rows[cursor->row-1],
			    &offset, &sz)) != NULL)
				buffer_insert_char(buffer, cursor->row,
				    &offset2, p, sz);

			buffer_erase_eol_at(buffer, cursor->row-1,
			    cursor->offset);

			cursor->col = 0;
			cursor->offset = 0;
			offset = 0;
		} else {
			if (buffer_insert_char(cursor->buffer, cursor->row,
			    &offset, &ch, 1) == -1)
				return -1;
		}
		s++;
	}
	cursor->offset = offset;

	broadcast_update(cursor->buffer,
	    from_row, 0, cursor->row, 0, BUFFER_UPDATE_LINE);
	return 0;
}

#if 0
static void
dump_buffer(struct buffer *buffer)
{
	size_t row, col;
	struct row *rowptr;

	assert(buffer != NULL);

	printf("DUMP_BUFFER %zu\n", buffer->n_rows);
	for (row = 0; row < buffer->n_rows; row++) {
		rowptr = &buffer->rows[row];
		printf("%zu (%zu / %zu): '", row, rowptr->bytes_used,
		    rowptr->bytes_size);
		for (col = 0; col < rowptr->bytes_used; col++)
			putchar(rowptr->bytes[col]);
		printf("'\n");
	}
}
#endif

static char *
row_at(struct row *rowptr, size_t *offset, size_t *sz_out)
{
	size_t begin;

	begin = *offset;
	if (utf8_incr_col(rowptr->bytes, rowptr->bytes_used, offset, NULL)
	    == 0)
		return NULL;

	assert(begin < *offset);
	*sz_out = *offset - begin;
	return &rowptr->bytes[begin];
}

size_t
buffer_bytes_at(struct buffer *buffer, size_t row)
{
	if (row >= buffer->n_rows)
		return 0;
	return buffer->rows[row].bytes_used;
}
