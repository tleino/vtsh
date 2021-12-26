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
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

struct dpy;
struct editor;
struct widget;
struct button;
struct pty;

typedef enum pty_action {
	PtyActionOpen,
	PtyActionClose
} PtyAction;

typedef void (*PtyActionCallback)(struct pty *, PtyAction, const char *,
    void *);

struct pty {
	struct widget *parent;
	struct widget *widget;

	pid_t pid;
	int ptyfd;

	FILE *fp;
	DIR *dp;
	char *file;
	int file_unsaved;

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

	PtyActionCallback ptyaction;
	void *ptyaction_udata;

	struct button *close_button;
};

struct pty	*pty_create(struct pty *, const char *, struct widget *);
void		 pty_run_command(struct pty *, const char *);
void		 pty_set_action_callback(struct pty *, PtyActionCallback,
		    void *);
void		 pty_free(struct pty *);
void		 pty_toggle_hide_output(struct pty *);
void		 pty_hide_output(struct pty *);
void		 pty_show_output(struct pty *);
void		 pty_save(struct pty *);

#endif
