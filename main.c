/*
 * vtsh - A mashup of virtual terminal and shell
 * Copyright (c) 2021-2022, Tommi Leino <namhas@gmail.com>
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
#include "ptylist.h"
#include "event.h"
#include "xevent.h"
#include "font.h"
#include "pty.h"

#include <err.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>

#ifdef HAVE_PLEDGE
#include <unistd.h>
#endif

struct dpy *dpy;
int running;

int
main(int argc, char *argv[])
{
	struct ptylist *ptylist;
	XEvent e;
	char *s;
	int i;
	size_t sz, alloc;

#ifdef HAVE_PLEDGE
	if (pledge("stdio cpath rpath wpath tty unix inet proc exec", NULL)
	    == -1)
		err(1, "pledge");
#endif

	if ((dpy = dpy_create()) == NULL)
		errx(1, "failed connecting to X11 server");

	if (!setlocale(LC_CTYPE, "en_US.UTF-8") || !XSupportsLocale())
		errx(1, "no locale support");
	else
		mbtowc(NULL, NULL, MB_CUR_MAX);

	if ((ptylist = ptylist_create("vtsh", NULL)) == NULL)
		err(1, "creating main window");

#ifdef HAVE_PLEDGE
	if (pledge("stdio cpath rpath wpath tty proc exec", NULL) == -1)
		err(1, "pledge");
#endif

	/*
	 * Wait until main window is mapped. This is not always strictly
	 * necessary.
	 */
	XSync(DPY(dpy), False);
	do {
		XMaskEvent(DPY(dpy), StructureNotifyMask, &e);
	} while (e.type != MapNotify);

	add_event_source(ConnectionNumber(DPY(dpy)), process_xevents, NULL);

	s = NULL;
	for (i = 1; i < argc; i++) {
		if (i == 1) {
			alloc = 256;
			s = malloc(alloc);
			if (s == NULL)
				err(1, "malloc");
			sz = 0;
		}

		if (argv[i] == NULL || argv[i][0] == '\0')
			break;
		if (sz + strlen(argv[i]) + 2 >= alloc) {
			alloc += sz + strlen(argv[i]) + 2;
			s = realloc(s, alloc);
			if (s == NULL)
				err(1, "realloc");
		}
		strcpy(&s[sz], argv[i]);
		sz += strlen(argv[i]);
		if (argc > 0 && i != argc-1)
			s[sz++] = ' ';
	}

	if (s != NULL) {
		run_event_loop();

		pty_run_command(ptylist_find_focus(ptylist), s);
		free(s);
		ptylist_toggle_focus_level(ptylist);
	}

	running = 1;
	while (running)
		run_event_loop();

	XSync(DPY(dpy), False);
	font_close();

	XSync(DPY(dpy), False);
	ptylist_free_all();

	XSync(DPY(dpy), False);
	dpy_free(dpy);
	return 0;
}
