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

#ifndef FONT_H
#define FONT_H

#include "fontnames.h"

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>

struct dpy;

int	 font_draw(Window, int, int, int, const char *, size_t);
void	 font_clear(Window, int, int, int);
void	 font_extents(const char *, size_t, XGlyphInfo *);
int	 font_str_width(int, const char *, size_t);
void	 font_set(int);
int	 font_height(void);
int	 font_width(void);
void	 font_set_fgcolor(int);
void	 font_set_bgcolor(int);
void	 font_close(void);
void	 font_destroy_ftdraw(void);

#endif
