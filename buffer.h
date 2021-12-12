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

#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>
#include <unistd.h>

struct buffer;

struct cursor {
	int row;
	int col;
	struct buffer *buffer;
};

#define CURSOR_COL(_x) (_x)->col
#define CURSOR_ROW(_x) (_x)->row
#define CURSOR_BUFFER(_x) (_x)->buffer

typedef enum buffer_update {
	BUFFER_UPDATE_LINE,
} BufferUpdate;

typedef void (*BLCallback)(int, int, int, int, BufferUpdate, void *);

struct buffer	*buffer_create(void);
void		 buffer_free(struct buffer *);
void		 buffer_clear(struct buffer *);

int		 buffer_add_listener(struct buffer *, BLCallback, void *);
void		 buffer_remove_listener(struct buffer *, BLCallback);

int		 buffer_row_uflags(struct buffer *, int);
void		 buffer_set_row_uflags(struct buffer *, int, int);

struct cursor	*buffer_cursor_create(struct buffer *);
void		 buffer_cursor_free(struct cursor *);
int		 buffer_insert(struct cursor *, const char *, size_t);
void		 buffer_erase(struct buffer *, struct cursor *);
void		 buffer_erase_eol(struct buffer *, struct cursor *);
void		 buffer_remove_row(struct buffer *, int);

void		 buffer_update_cursor(struct buffer *, struct cursor *,
		    int, int);
void		 buffer_clear_row(struct buffer *, int);

size_t		 buffer_cols(struct buffer *, size_t);

size_t		 buffer_rows(struct buffer *);
wchar_t		 buffer_at(struct buffer *, size_t, size_t);

size_t		 buffer_u8str_at(struct buffer *, size_t, size_t,
		    ssize_t, char *, size_t);

#endif
