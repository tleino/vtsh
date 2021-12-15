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

#ifndef PTY_H
#define PTY_H

#include <unistd.h>

struct dpy;
struct editor;
struct widget;

struct pty {
	struct widget *parent;
	struct widget *widget;

	pid_t pid;
	int ptyfd;

	struct layout *hbox;
	struct layout *vbox;

	struct buffer *cmd_buffer;
	struct cursor *cmd_cursor;
	struct editor *cmd_editor;

	struct buffer *ts_buffer;
	struct cursor *ts_icursor;
	struct cursor *ts_ocursor;
	struct editor *ts_editor;

	struct statbar *statbar;
	struct label *cwd;

	struct pty *master;
	struct pty **slaves;
	struct pty *active_slave;
	int n_slaves;
	int max_slaves;
};

struct pty	*pty_create(struct pty *, const char *, struct widget *);
void		 pty_free(struct pty *);
void		 pty_toggle_hide_output(struct pty *);
void		 pty_hide_output(struct pty *);
void		 pty_show_output(struct pty *);

#endif
