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

#ifndef EDITOR_H
#define EDITOR_H

#include <X11/Xlib.h>

struct cursor;
struct dpy;
struct widget;


typedef void (*EditSubmitHandler)(const char *, void *);
typedef int (*EditResizeHandler)(Window, int *, int *, void *);

struct editor {
	Window			 window;
	GC			 gc;
	struct buffer		*buffer;
	struct cursor		*cursor;
	struct cursor		*ocursor;
	EditSubmitHandler	 submit;
	void			*submit_udata;
	EditResizeHandler	 resize;
	void			*resize_udata;
	int			 focused;
	int			 bgcolor;
	int			 max_rows;
	struct dpy		*dpy;
	int			 old_height;
	int			 top_row;
	int			 bottom_row;
	size_t			 begin_colx;	/* TODO */
	size_t			 begin_col;
	int			 largest_height;

	struct widget		*widget;
};

int		 editor_max_height(struct editor *);
void		 editor_set_cursor(struct editor *, struct cursor *,
		    struct cursor *);
void		 editor_set_resize_handler(struct editor *,
		    EditResizeHandler, void *);
struct editor	*editor_create(struct dpy *, struct cursor *,
		    EditSubmitHandler, void *, int, int, const char *,
		    struct widget *);
void		 editor_shrink(struct editor *);
void		 editor_free(struct editor *);

#endif
