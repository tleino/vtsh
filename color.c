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

#include "color.h"
#include "dpy.h"

#include <assert.h>
#include <err.h>

static XColor color[NUM_COLOR];
static int has_color[NUM_COLOR];

#include "colornames.c"

XColor
query_color(struct dpy *dpy, int i)
{
	XColor def, exact;

	assert(i < NUM_COLOR);

	if (has_color[i])
		return color[i];

	if (XAllocNamedColor(DPY(dpy), XDefaultColormap(DPY(dpy),
	    DPY_SCREEN(dpy)), colorname[i], &def, &exact) == 0)
		errx(1, "couldn't allocate '%s'", colorname[i]);

	color[i] = def;
	has_color[i] = 1;

	return color[i];
}
