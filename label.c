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

#include "label.h"
#include "widget.h"
#include "font.h"
#include "color.h"
#include "dpy.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void	label_draw(int, int, int, int, void *);

struct label *
label_create(const char *name, struct widget *parent)
{
	struct label *label;

	if ((label = calloc(1, sizeof(*label))) == NULL)
		return NULL;

	if ((WIDGET(label) = widget_create(name, parent)) == NULL)
		goto fail;

	font_set(FONT_NORMAL);
	WIDGET_PREFER_HEIGHT(label) = font_height();
	WIDGET_PREFER_WIDTH(label) = parent->size[WIDTH_AXIS] / 4;

	widget_set_draw_callback(WIDGET(label), label_draw, label);

	widget_show(WIDGET(label));

	return label;
fail:
	label_free(label);
	return NULL;
}

void
label_set(struct label *label, const char *text)
{
	XGlyphInfo extents;
	extern struct dpy *dpy;
	size_t len;

	if (label->text != NULL)
		free(label->text);

	len = strlen(text) + 2 + 1;
	label->text = malloc(len);
	if (label->text == NULL)
		return;
	snprintf(label->text, len, " %s ", text);
	label->len = strlen(label->text);

	font_extents(label->text, label->len, &extents);
	label->px_len = extents.xOff;

	WIDGET_PREFER_WIDTH(label) = label->px_len;
	WIDGET_PREFER_HEIGHT(label) = font_height();
	widget_update_geometry(WIDGET(label));

	label_draw(0, 0, WIDGET_WIDTH(label), WIDGET_HEIGHT(label), label);
	XFlush(DPY(dpy));
}

void
label_free(struct label *label)
{
	if (label->text != NULL)
		free(label->text);
	if (label->widget != NULL)
		widget_free(label->widget);
	free(label);
}

static void
label_draw(int x, int y, int width, int height, void *udata)
{
	struct label *label = udata;

	font_set_fgcolor(COLOR_FLAGS);
	font_set_bgcolor(COLOR_TITLE_BG_NORMAL);
	x = font_draw(WINDOW(label), 0, 0, label->text, strlen(label->text));
	font_clear(WINDOW(label), x, 0, WIDGET_WIDTH(label) - x);
}
