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
#include "label.h"

#include <stdlib.h>
#include <stdio.h>

struct statbar *
statbar_create(const char *name, struct widget *parent)
{
	struct statbar *statbar;

	if ((statbar = calloc(1, sizeof(struct statbar))) == NULL)
		return NULL;

	if ((statbar->label = label_create(name, parent)) == NULL)
		goto fail;
	WIDGET(statbar) = WIDGET(statbar->label);
	return statbar;
fail:
	statbar_free(statbar);
	return NULL;
}

void
statbar_update_status(struct statbar *statbar, StatbarState state,
	int pid, int ret, int lines)
{
	char status[256], str[256];

	if (pid != 0)
		snprintf(status, sizeof(status), "%dL %d", lines, pid);
	else if (state == STATBAR_STATE_EXITED)
		snprintf(status, sizeof(status), "%dL E%d", lines, ret);
	else if (state == STATBAR_STATE_SIGNALED)
		snprintf(status, sizeof(status), "%dL S%d", lines, ret);
	else if (state == STATBAR_STATE_FILE_SAVED)
		snprintf(status, sizeof(status), "%dL", lines);
	else if (state == STATBAR_STATE_FILE_UNSAVED)
		snprintf(status, sizeof(status), "%dL *", lines);
	else
		snprintf(status, sizeof(status), "%dL", lines);

	snprintf(str, sizeof(str), "%-12s", status);
	label_set(statbar->label, str);
}

void
statbar_free(struct statbar *statbar)
{
	if (statbar->label != NULL)
		label_free(statbar->label);
	free(statbar);
}
