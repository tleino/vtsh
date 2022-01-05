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

#include "button.h"
#include "label.h"
#include "widget.h"

#include <stdlib.h>

static int button_mousepress(struct widget *, XButtonEvent *, void *);

struct button *
button_create(const char *label, ButtonCallback callback, void *udata,
    const char *name, struct widget *parent)
{
	struct button *button;

	button = calloc(1, sizeof(struct button));
	if (button == NULL)
		return NULL;

	button->label = label_create(name, parent);
	label_set(button->label, label);
	if (button->label == NULL)
		goto fail;
	button->widget = WIDGET(button->label);

	button->callback = callback;
	button->callback_udata = udata;

	widget_set_mousepress_callback(WIDGET(button), button_mousepress,
	    button);

	return button;
fail:
	button_free(button);
	return NULL;
}

void
button_free(struct button *button)
{
	if (button->label != NULL)
		label_free(button->label);
}

static int
button_mousepress(struct widget *widget, XButtonEvent *xbutton, void *udata)
{
	struct button *button = udata;

	if (xbutton->type != ButtonRelease && !button->act_on_release)
		return 0;

	button->callback(button, button->callback_udata);
	return 1;
}
