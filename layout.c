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

#include "layout.h"
#include "widget.h"
#include "util.h"
#include "dpy.h"
#include "font.h"

#include <stdlib.h>
#include <assert.h>

static void		 layout_update_geometry(void *);
static struct layout	*layout_create(LayoutType, const char *,
			    struct widget *);

static struct layout *
layout_create(LayoutType type, const char *name, struct widget *parent)
{
	struct layout *layout;

	assert(type >= 0 && type < NUM_LAYOUT_TYPE);

	if ((layout = calloc(1, sizeof(*layout))) == NULL)
		return NULL;
	layout->type = type;

	if ((WIDGET(layout) = widget_create_windowless(name, parent)) == NULL)
		goto fail;

	widget_set_geometry_callback(WIDGET(layout), layout_update_geometry,
	    layout);
	widget_show(WIDGET(layout));
	return layout;
fail:
	layout_free(layout);
	return NULL;
}

struct layout *
layout_create_vbox(const char *name, struct widget *parent)
{
	return layout_create(LAYOUT_TYPE_VBOX, name, parent);
}

struct layout *
layout_create_hbox(const char *name, struct widget *parent)
{
	return layout_create(LAYOUT_TYPE_HBOX, name, parent);
}

static void
layout_update_geometry(void *udata)
{
	struct layout *layout = udata;
	int i;
	int offset;
	int equal, equal_surplus;
	int surplus;
	int n_need, d, add, n;
	extern struct dpy *dpy;
	unsigned int mask;
	XWindowChanges changes;
	int sides[100] = { 0 };	/* TODO: Make dynamic */
	int axis;

	font_set(FONT_NORMAL);

	switch (layout->type) {
	case LAYOUT_TYPE_VBOX:
		axis = HEIGHT_AXIS;
		break;
	case LAYOUT_TYPE_HBOX:
		axis = WIDTH_AXIS;
		break;
	default:
		assert(0);
	}

	n = 0;
	for (i = 0; i < NCHILDREN(layout); i++)
		if (CHILD(layout, i)->visible)
			n++;

	if (n == 0)
		return;

	for (i = 0; i < NCHILDREN(layout); i++)
		if (CHILD(layout, i)->prefer_size[axis] == 0)
			CHILD(layout, i)->prefer_size[axis] = font_height();

	equal = WIDGET(layout)->size[axis] / n;
	surplus = 0;
	n_need = 0;
	for (i = 0; i < NCHILDREN(layout); i++) {
		if (!CHILD(layout, i)->visible)
			continue;
		if (CHILD(layout, i)->prefer_size[axis] < equal)
			surplus += equal - CHILD(layout, i)->prefer_size[axis];
		else if (CHILD(layout, i)->prefer_size[axis] > equal)
			n_need++;

		sides[i] = MIN(equal, CHILD(layout, i)->prefer_size[axis]);
	}

	while (n_need > 0) {
		equal_surplus = surplus / n_need;
		if (equal_surplus == 0)
			break;

		for (i = 0; i < NCHILDREN(layout); i++) {
			if (!CHILD(layout, i)->visible)
				continue;
			d = MAX((CHILD(layout, i)->prefer_size[axis] -
			    sides[i]), 0);
			if (d > 0) {
				add = MIN(equal_surplus, d);
				sides[i] += add;
				surplus -= add;
				d = MAX((CHILD(layout, i)->prefer_size[axis] -
				    sides[i]), 0);
				if (d == 0)
					n_need--;
			}
		}
	}

	offset = 0;
	for (i = 0; i < NCHILDREN(layout);
	    offset += CHILD(layout, i)->size[axis], i++) {
		if (!CHILD(layout, i)->visible)
			continue;

		CHILD(layout, i)->has_managed_geometry = 1;

		CHILD(layout, i)->pos[axis] = offset;
		CHILD(layout, i)->pos[!axis] = 0;
		CHILD(layout, i)->size[axis] = sides[i];
		CHILD(layout, i)->size[!axis] = WIDGET(layout)->size[!axis];

		if (CHILD(layout, i)->parent->window == 0) {
			POSX(CHILD(layout, i)) +=
			    POSX(CHILD(layout, i)->parent);
			POSY(CHILD(layout, i)) +=
			    POSY(CHILD(layout, i)->parent);
		}

		mask = 0;
		if (POSX(CHILD(layout, i)) != OLD_POSX(CHILD(layout, i))) {
			mask |= CWX;
			changes.x = POSX(CHILD(layout, i));
		}
		if (POSY(CHILD(layout, i)) != OLD_POSY(CHILD(layout, i))) {
			mask |= CWY;
			changes.y = POSY(CHILD(layout, i));
		}
		if (WIDTH(CHILD(layout, i)) != OLD_WIDTH(CHILD(layout, i))) {
			mask |= CWWidth;
			changes.width = WIDTH(CHILD(layout, i));
		}
		if (HEIGHT(CHILD(layout, i)) != OLD_HEIGHT(CHILD(layout, i))) {
			mask |= CWHeight;
			changes.height = HEIGHT(CHILD(layout, i));
		}

		if (CHILD(layout, i)->window != 0 && mask > 0)
			XConfigureWindow(DPY(dpy),
				    CHILD(layout, i)->window, mask, &changes);
	}
	XFlush(DPY(dpy));
}

void
layout_free(struct layout *layout)
{
	if (layout->widget != NULL)
		widget_free(layout->widget);
	free(layout);
}
