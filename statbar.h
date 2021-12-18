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

#ifndef STATBAR_H
#define STATBAR_H

struct widget;

struct statbar {
	struct widget *widget;
	struct label *label;
};

typedef enum statbar_state {
	STATBAR_STATE_NOT_STARTED,
	STATBAR_STATE_STARTED,
	STATBAR_STATE_FILE_SAVED,
	STATBAR_STATE_FILE_UNSAVED,
	STATBAR_STATE_EXITED,
	STATBAR_STATE_SIGNALED
} StatbarState;

struct statbar	*statbar_create(const char *, struct widget *);
void		 statbar_free(struct statbar *);
void		 statbar_update_status(struct statbar *, StatbarState,
		    int, int, int);

#endif
