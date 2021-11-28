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

#include "dpy.h"

#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

struct dpy *
dpy_create()
{
	char *denv;
	struct dpy *dpy;

	if ((dpy = calloc(1, sizeof(struct dpy))) == NULL)
		return NULL;

	denv = getenv("DISPLAY");
	if (denv == NULL && errno != 0)
		goto fail;

	if ((dpy->display = XOpenDisplay(denv)) == NULL)
		goto fail;

	/*
	 * Prevent X11 connection being copied to child processes.
	 */
	if ((fcntl(ConnectionNumber(dpy), F_SETFD, FD_CLOEXEC)) == -1)
		goto fail;

	dpy->screen = DefaultScreen(dpy->display);
	dpy->root = DefaultRootWindow(dpy->display);

	return dpy;
fail:
	dpy_free(dpy);
	return NULL;
}

void
dpy_free(struct dpy *dpy)
{
	if (dpy->display != NULL)
		XCloseDisplay(dpy->display);
	free(dpy);
}
