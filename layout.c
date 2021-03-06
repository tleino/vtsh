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
#include "config.h"

#include <stdlib.h>
#include <assert.h>

static void		 layout_update_geometry(void *);
static void		 layout_update_prefer(void *);
static struct layout	*layout_create(LayoutType, const char *,
			    struct widget *);
static int		 layout_axis(struct layout *);

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
	widget_set_update_prefer_callback(WIDGET(layout), layout_update_prefer,
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

static int
layout_axis(struct layout *layout)
{
	int axis;

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

	return axis;

}

static void
layout_update_prefer(void *udata)
{
	struct layout *layout = udata;
	int i, axis;

	axis = layout_axis(layout);
	WIDGET(layout)->prefer_size[axis] = 0;
	WIDGET(layout)->prefer_size[!axis] = 0;

	for (i = 0; i < NCHILDREN(layout); i++) {
		WIDGET(layout)->prefer_size[!axis] =
		    MAX(WIDGET(layout)->prefer_size[!axis],
		    CHILD(layout, i)->prefer_size[!axis]);

		if (!CHILD(layout, i)->visible)
			continue;

		WIDGET(layout)->prefer_size[axis] +=
		    CHILD(layout, i)->prefer_size[axis];
	}
}

static void
layout_update_geometry(void *udata)
{
	struct layout *layout = udata;
#ifdef WANT_OVERLAPPING_WINDOWS
	struct widget *root;
#endif
	int i;
	int offset;
	double equal, equal_surplus, surplus, d, add, n_need, n;
	double sides[100] = { 0 };	/* TODO: Make dynamic */
	int axis;

	font_set(FONT_NORMAL);

	if (!WIDGET(layout)->visible)
		return;

	axis = layout_axis(layout);
	n = 0;
	for (i = 0; i < NCHILDREN(layout); i++)
		if (CHILD(layout, i)->visible)
			n++;

	if (n == 0)
		return;

#ifdef WANT_OVERLAPPING_WINDOWS
	root = widget_find_root(WIDGET(layout));
#endif

	for (i = 0; i < NCHILDREN(layout); i++) {
		if (!CHILD(layout, i)->visible)
			continue;
		if (CHILD(layout, i)->prefer_size[axis] == 0)
			CHILD(layout, i)->prefer_size[axis] = font_height();
	}

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
	for (i = 0; i < NCHILDREN(layout); i++) {
		if (!CHILD(layout, i)->visible)
			continue;

		CHILD(layout, i)->has_managed_geometry = 1;

		CHILD(layout, i)->pos[axis] = offset;
		CHILD(layout, i)->pos[!axis] = 0;
		CHILD(layout, i)->size[axis] = sides[i];
		CHILD(layout, i)->size[!axis] = WIDGET(layout)->size[!axis];

#ifdef WANT_OVERLAPPING_WINDOWS
		CHILD(layout, i)->physical_size[axis] = root->size[axis];
		CHILD(layout, i)->physical_size[!axis] =
		    CHILD(layout, i)->size[!axis];
#endif

		offset += CHILD(layout, i)->size[axis];

		if (CHILD(layout, i)->parent->window == 0) {
			POSX(CHILD(layout, i)) +=
			    POSX(CHILD(layout, i)->parent);
			POSY(CHILD(layout, i)) +=
			    POSY(CHILD(layout, i)->parent);
		}

		CHILD(layout, i)->changes.x = POSX(CHILD(layout, i));
		CHILD(layout, i)->changes.y = POSY(CHILD(layout, i));
#ifndef WANT_OVERLAPPING_WINDOWS
		CHILD(layout, i)->changes.width = WIDTH(CHILD(layout, i));
		CHILD(layout, i)->changes.height = HEIGHT(CHILD(layout, i));
#else
		CHILD(layout, i)->changes.width =
		    CHILD(layout, i)->physical_size[0];
		CHILD(layout, i)->changes.height =
		    CHILD(layout, i)->physical_size[1];
#endif

		if (POSX(CHILD(layout, i)) != OLD_POSX(CHILD(layout, i)))
			CHILD(layout, i)->changes_mask |= CWX;
		if (POSY(CHILD(layout, i)) != OLD_POSY(CHILD(layout, i)))
			CHILD(layout, i)->changes_mask |= CWY;

#ifndef WANT_OVERLAPPING_WINDOWS
		if (WIDTH(CHILD(layout, i)) != OLD_WIDTH(CHILD(layout, i)))
			CHILD(layout, i)->changes_mask |= CWWidth;
		if (HEIGHT(CHILD(layout, i)) != OLD_HEIGHT(CHILD(layout, i)))
			CHILD(layout, i)->changes_mask |= CWHeight;
#else
		if (CHILD(layout, i)->physical_size[0] !=
		    CHILD(layout, i)->old_physical_size[0])
			CHILD(layout, i)->changes_mask |= CWWidth;
		if (CHILD(layout, i)->physical_size[1] !=
		    CHILD(layout, i)->old_physical_size[1])
			CHILD(layout, i)->changes_mask |= CWHeight;

#endif
	}
}

void
layout_free(struct layout *layout)
{
	if (layout->widget != NULL)
		widget_free(layout->widget);
	free(layout);
}
