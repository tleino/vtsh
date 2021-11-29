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

#include "statbar.h"
#include "widget.h"
#include "font.h"
#include "color.h"
#include "dpy.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct statbar {
	struct widget *widget;
	char *status;
};

static void	statbar_draw(int, int, int, int, void *);

struct statbar *
statbar_create(const char *name, struct widget *parent)
{
	struct statbar *statbar;

	if ((statbar = calloc(1, sizeof(struct statbar))) == NULL)
		return NULL;

	if ((WIDGET(statbar) = widget_create(name, parent)) == NULL)
		goto fail;

	font_set(FONT_NORMAL);
	WIDGET_PREFER_HEIGHT(statbar) = font_height();
	WIDGET_PREFER_WIDTH(statbar) = parent->size[WIDTH_AXIS] / 4;

	widget_set_draw_callback(WIDGET(statbar), statbar_draw, statbar);

	widget_show(WIDGET(statbar));

	return statbar;
fail:
	statbar_free(statbar);
	return NULL;
}

void
statbar_update_status(struct statbar *statbar, StatbarState state,
	int pid, int ret, int lines)
{
	extern struct dpy *dpy;
	char status[256];

	if (pid != 0)
		snprintf(status, sizeof(status),
		    "%dL PID %d", lines, pid);
	else if (state == STATBAR_STATE_EXITED)
		snprintf(status, sizeof(status),
		    "%dL exit %d", lines, ret);
	else if (state == STATBAR_STATE_SIGNALED)
		snprintf(status, sizeof(status),
		    "%dL signal %d", lines, ret);
	else
		snprintf(status, sizeof(status),
		    "%dL", lines);

	if (statbar->status != NULL)
		free(statbar->status);
	statbar->status = strdup(status);
	statbar_draw(0, 0, WIDGET(statbar)->size[WIDTH_AXIS],
	    WIDGET(statbar)->size[HEIGHT_AXIS], statbar);
	XFlush(DPY(dpy));
}

struct widget *
statbar_widget(struct statbar *statbar)
{
	return statbar->widget;
}

void
statbar_free(struct statbar *statbar)
{
	if (statbar->widget != NULL)
		widget_free(statbar->widget);
	free(statbar);
}

static void
statbar_draw(int x, int y, int width, int height, void *udata)
{
	struct statbar *statbar = udata;

	font_set_fgcolor(COLOR_FLAGS);
	font_set_bgcolor(COLOR_TITLE_BG_NORMAL);
	x = font_draw(WINDOW(statbar), 0, 0, statbar->status,
		strlen(statbar->status));
	font_clear(WINDOW(statbar), x, 0,
	    WIDGET(statbar)->size[WIDTH_AXIS] - x);
}
