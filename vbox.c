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

#include "vbox.h"
#include "widget.h"
#include "util.h"
#include "dpy.h"

#include <stdlib.h>

static void	vbox_update_geometry(void *);

struct vbox *
vbox_create(const char *name, struct widget *parent)
{
	struct vbox *vbox;

	if ((vbox = calloc(1, sizeof(*vbox))) == NULL)
		return NULL;

	if ((WIDGET(vbox) = widget_create_windowless(name, parent)) == NULL)
		goto fail;

	widget_set_geometry_callback(WIDGET(vbox), vbox_update_geometry,
	    vbox);
	widget_show(WIDGET(vbox));
	return vbox;
fail:
	vbox_free(vbox);
	return NULL;
}

static void
vbox_update_geometry(void *udata)
{
	struct vbox *vbox = udata;
	int i;
	int y;
	int equal, equal_surplus;
	int surplus;
	int n_need, d, add, n;
	extern struct dpy *dpy;
	unsigned int mask;
	XWindowChanges changes;
	int heights[100] = { 0 };	/* TODO: Make dynamic */

	n = 0;
	for (i = 0; i < NCHILDREN(vbox); i++)
		if (CHILD(vbox, i)->visible)
			n++;

	if (n == 0)
		return;

	for (i = 0; i < NCHILDREN(vbox); i++)
		if (CHILD(vbox, i)->prefer_height == 0)
			CHILD(vbox, i)->prefer_height = 28;

	equal = WIDGET(vbox)->height / n;
	surplus = 0;
	n_need = 0;
	for (i = 0; i < NCHILDREN(vbox); i++) {
		if (!CHILD(vbox, i)->visible)
			continue;
		if (CHILD(vbox, i)->prefer_height < equal)
			surplus += equal - CHILD(vbox, i)->prefer_height;
		else if (CHILD(vbox, i)->prefer_height > equal)
			n_need++;

		heights[i] = MIN(equal, CHILD(vbox, i)->prefer_height);
	}

	while (n_need > 0) {
		equal_surplus = surplus / n_need;
		if (equal_surplus == 0)
			break;

		for (i = 0; i < NCHILDREN(vbox); i++) {
			if (!CHILD(vbox, i)->visible)
				continue;
			d = MAX(
			    (CHILD(vbox, i)->prefer_height - heights[i]),
			    0);
			if (d > 0) {
				add = MIN(equal_surplus, d);
				heights[i] += add;
				surplus -= add;
				d = MAX(
				    (CHILD(vbox, i)->prefer_height -
				    heights[i]), 0);
				if (d == 0)
					n_need--;
			}
		}
	}

	y = 0;
	for (i = 0; i < NCHILDREN(vbox); y += CHILD(vbox, i)->height, i++) {
		if (!CHILD(vbox, i)->visible)
			continue;

		CHILD(vbox, i)->has_managed_geometry = 1;

		CHILD(vbox, i)->x = 0;
		CHILD(vbox, i)->y = y;
		CHILD(vbox, i)->width = WIDGET(vbox)->width;
		CHILD(vbox, i)->height = heights[i];

		mask = 0;
		if (CHILD(vbox, i)->x != CHILD(vbox, i)->old_x) {
			mask |= CWX;
			changes.x = CHILD(vbox, i)->x;
		}
		if (CHILD(vbox, i)->y != CHILD(vbox, i)->old_y) {
			mask |= CWY;
			changes.y = CHILD(vbox, i)->y;
		}
		if (CHILD(vbox, i)->width != CHILD(vbox, i)->old_width) {
			mask |= CWWidth;
			changes.width = CHILD(vbox, i)->width;
		}
		if (CHILD(vbox, i)->height != CHILD(vbox, i)->old_height) {
			mask |= CWHeight;
			changes.height = CHILD(vbox, i)->height;
		}

		if (CHILD(vbox, i)->window != 0)
			if (mask > 0) {
				XConfigureWindow(DPY(dpy),
				    CHILD(vbox, i)->window, mask, &changes);
			}
	}
	XFlush(DPY(dpy));
}

void
vbox_free(struct vbox *vbox)
{
	if (vbox->widget != NULL)
		widget_free(vbox->widget);
	free(vbox);
}
