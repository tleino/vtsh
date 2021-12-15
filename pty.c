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

#include "pty.h"
#include "buffer.h"
#include "editor.h"
#include "color.h"
#include "event.h"
#include "statbar.h"
#include "layout.h"
#include "widget.h"
#include "label.h"
#include "uflags.h"

#ifdef HAVE_PTY_H
#include <pty.h>
#else
#include <util.h>
#endif

#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <termios.h>
#include <limits.h>
#include <stdio.h>

struct editor;

static int	pty_create_cmd(struct pty *);
static int	pty_create_ts(struct pty *);
static void	pty_submit_command(const char *, void *);
static void	pty_submit_stdin(const char *, void *);
static void	pty_process_events(int, void *);

static int	pty_add_slave(struct pty *, struct pty *);
static int	pty_find_slave(struct pty *, struct pty *);
static void	pty_remove_slave(struct pty *, struct pty *);

struct pty *
pty_create(struct pty *master, const char *name, struct widget *parent)
{
	struct pty *pty;
	char cwd[PATH_MAX];

	if ((pty = calloc(1, sizeof(struct pty))) == NULL)
		return NULL;
	pty->parent = parent;
	pty->ptyfd = -1;

	if (master != NULL)
		if (pty_add_slave(master, pty) == -1)
			goto fail;

	pty->vbox = layout_create_vbox(name, parent);
	pty->widget = WIDGET(pty->vbox);
	pty->hbox = layout_create_hbox("hbox", pty->widget);

	if ((pty->cwd = label_create("cwd", WIDGET(pty->hbox))) == NULL)
		goto fail;

	if (getcwd(cwd, sizeof(cwd)) != NULL)
		label_set(pty->cwd, cwd);
	else
		warn("getting cwd");

	if (pty_create_cmd(pty) == -1) {
		warn("pty_create_cmd failed");
		goto fail;
	}
	if (pty_create_ts(pty) == -1) {
		warn("pty_create_ts failed");
		goto fail;
	}

	if ((pty->statbar = statbar_create("statbar", WIDGET(pty->hbox)))
	    == NULL)
		goto fail;

	statbar_update_status(pty->statbar, STATBAR_STATE_NOT_STARTED, 0, 0, 0);
	return pty;

fail:
	pty_free(pty);
	return NULL;
}

void
pty_toggle_hide_output(struct pty *pty)
{
	if (pty->ts_editor) {
		if (WIDGET(pty->ts_editor)->visible)
			widget_hide(WIDGET(pty->ts_editor));
		else
			widget_show(WIDGET(pty->ts_editor));
	}
}

void
pty_hide_output(struct pty *pty)
{
	if (pty->ts_editor) {
		widget_hide(WIDGET(pty->ts_editor));
	}
}

static void
pty_process_events(int ptyfd, void *udata)
{
	int n;
	static char buf[8192];
	struct pty *master = udata, *pty;
	int status;
	int state;

	n = read(ptyfd, buf, sizeof(buf));

	if (n > 0 && master->active_slave != NULL)
		pty = master->active_slave;
	else
		pty = master;

	if (n <= 0 && master->n_slaves > 0)
		while (master->n_slaves)
			pty_remove_slave(master,
			    master->slaves[master->n_slaves-1]);

	if (n > 0) {
		buffer_insert(pty->ts_ocursor, buf, n);
		statbar_update_status(pty->statbar, STATBAR_STATE_STARTED,
		    pty->pid, 0, buffer_rows(pty->ts_buffer));
	} else {
		remove_event_source(ptyfd);
		close(ptyfd);
		pty->ptyfd = -1;

		status = 0;
		state = STATBAR_STATE_STARTED;
		if (waitpid(pty->pid, &status, 0) == pty->pid) {
			pty->pid = 0;

			if (WIFSIGNALED(status)) {
				state = STATBAR_STATE_SIGNALED;
				status = WTERMSIG(status);
			} else if (WIFEXITED(status)) {
				state = STATBAR_STATE_EXITED;
				status = WEXITSTATUS(status);
			}
		}

		statbar_update_status(pty->statbar, state, pty->pid,
		    status, buffer_rows(pty->ts_buffer));
	}
}

static void
pty_submit_stdin(const char *s, void *udata)
{
	struct pty *pty = udata;
	int i, rows, n_overwrite;

	if (pty->ptyfd != -1) {
		buffer_clear_row(pty->ts_buffer, pty->ts_icursor->row);
		buffer_set_row_uflags(pty->ts_buffer, pty->ts_icursor->row,
		    ROW_UFLAGS_CMDLINE);
		pty->ts_icursor->col = 0;
		pty->ts_ocursor->row = pty->ts_icursor->row;
		pty->ts_ocursor->col = pty->ts_icursor->col;

		rows = buffer_rows(pty->ts_buffer);
		n_overwrite = 0;
		for (i = pty->ts_icursor->row+1; i < rows; i++) {
			if (buffer_row_uflags(pty->ts_buffer, i) &
			    ROW_UFLAGS_CMDLINE)
				break;
			n_overwrite++;
		}
		while (n_overwrite--)
			buffer_remove_row(pty->ts_buffer,
			    pty->ts_icursor->row+1);
	
		write(pty->ptyfd, s, strlen(s));
		write(pty->ptyfd, "\n", 1);
	} else
		buffer_insert(pty->ts_icursor, "\n", 1);
}

static void
pty_submit_command(const char *s, void *udata)
{
	int status;
	char *sh;
	struct pty *pty = udata, *master;
	struct termios ts;

	master = pty->master;
	if (master != NULL) {
		master->active_slave = pty;

		buffer_clear(pty->ts_buffer);
		pty->ts_icursor->row = 0;
		pty->ts_icursor->col = 0;
		pty->ts_ocursor->row = 0;
		pty->ts_ocursor->col = 0;
		write(master->ptyfd, s, strlen(s));
		write(master->ptyfd, "\n", 1);
		return;
	}

	if (pty->pid > 0) {
		while (pty->n_slaves)
			pty_remove_slave(pty,
			    pty->slaves[pty->n_slaves-1]);

		remove_event_source(pty->ptyfd);
		close(pty->ptyfd);
		pty->ptyfd = -1;
		kill(pty->pid, 9);
		waitpid(pty->pid, &status, 0);
		pty->pid = 0;
	}

	/*
	 * Clear previous buffer.
	 */
	if (pty->ts_buffer != NULL) {
		buffer_cursor_free(pty->ts_icursor);
		buffer_cursor_free(pty->ts_ocursor);
		buffer_free(pty->ts_buffer);
		if ((pty->ts_buffer = buffer_create()) == NULL)
			err(1, "buffer");
		if ((pty->ts_icursor = buffer_cursor_create(pty->ts_buffer))
		    == NULL)
			err(1, "input_cursor");
		if ((pty->ts_ocursor = buffer_cursor_create(pty->ts_buffer))
		    == NULL)
			err(1, "output_cursor");

		editor_set_cursor(pty->ts_editor, pty->ts_icursor,
		    pty->ts_ocursor);
	}

	sh = getenv("SHELL");
	if (sh == NULL || sh[0] == '\0')
		sh = "/bin/sh";

	memset(&ts, '\0', sizeof(struct termios));
	/*
	 * We take OpenBSD defaults as the base and make some minor
	 * adjustments.
	 */
	ts.c_lflag = (ICANON | ISIG | IEXTEN | ECHO | ECHOE);
	ts.c_lflag &= ~(ECHO);
	ts.c_iflag = (IXON | IXANY | IMAXBEL | BRKINT | IGNCR);
	ts.c_iflag |= IGNCR;
	ts.c_iflag &= ~(ICRNL);
	ts.c_oflag |= (OPOST | OCRNL);
	ts.c_oflag &= ~(OCRNL);
	ts.c_cflag = (CREAD | CS8 | HUPCL);
	ts.c_cc[VMIN] = 1;
	ts.c_cc[VTIME] = 0;
	ts.c_ispeed = B115200;
	ts.c_ospeed = B115200;

	pty->pid = forkpty(&pty->ptyfd, NULL, &ts, NULL);
	if (pty->pid < 0)
		err(1, "forkpty");

	if (pty->pid == 0) {
		/*
		 * We don't support traditional vt-control sequences.
		 */
		setenv("TERM", "dumb", 1);
		setenv("PS1", "\\$ ", 1);
		setenv("PAGER", "cat", 1);
		if (execlp(sh, sh, "-c", s, NULL) == -1)
			err(1, "execlp");
	}

	add_event_source(pty->ptyfd, pty_process_events, pty);

	statbar_update_status(pty->statbar, STATBAR_STATE_STARTED,
	    pty->pid, status, buffer_rows(pty->ts_buffer));
}

static int
pty_create_cmd(struct pty *pty)
{
	extern struct dpy *dpy;

	if ((pty->cmd_buffer = buffer_create()) == NULL)
		return -1;

	if ((pty->cmd_cursor = buffer_cursor_create(pty->cmd_buffer)) == NULL)
		return -1;

	if ((pty->cmd_editor = editor_create(dpy,
	    pty->cmd_cursor,
	    pty_submit_command, pty, COLOR_TITLE_BG_NORMAL, 1,
	    "cmd_editor", WIDGET(pty->hbox))) == NULL)
		return -1;

	WIDGET(pty->cmd_editor)->level = 0;
	return 0;
}

static int
pty_create_ts(struct pty *pty)
{
	extern struct dpy *dpy;

	if ((pty->ts_buffer = buffer_create()) == NULL)
		return -1;
	if ((pty->ts_icursor = buffer_cursor_create(pty->ts_buffer)) == NULL)
		return -1;
	if ((pty->ts_ocursor = buffer_cursor_create(pty->ts_buffer)) == NULL)
		return -1;

	if ((pty->ts_editor = editor_create(dpy,
	    pty->ts_icursor,
	    pty_submit_stdin, pty, COLOR_TEXT_BG, -1, "ts_editor",
	    pty->widget)) == NULL)
		return -1;

	WIDGET(pty->ts_editor)->level = 1;
	return 0;
}

static int
pty_add_slave(struct pty *pty, struct pty *slave)
{
	void *tmp;
	size_t sz;

	if (pty->n_slaves == pty->max_slaves) {
		sz = (pty->max_slaves+1) * sizeof(struct pty *);
		tmp = realloc(pty->slaves, sz);
		if (tmp == NULL)
			return -1;
		pty->slaves = tmp;
		pty->max_slaves++;
	}

	pty->slaves[pty->n_slaves++] = slave;
	slave->master = pty;
	pty->active_slave = slave;

	return 0;
}

static int
pty_find_slave(struct pty *pty, struct pty *slave)
{
	int i;

	for (i = 0; i < pty->n_slaves; i++)
		if (pty->slaves[i] == slave)
			return i;

	return -1;
}

static void
pty_remove_slave(struct pty *pty, struct pty *slave)
{
	int i;
	void *dst, *src;
	size_t len;

	if ((i = pty_find_slave(pty, slave)) == -1) {
		warnx("did not find slave %lx", (intptr_t) slave);
		return;
	}

	if (pty->active_slave == slave)
		pty->active_slave = NULL;
	slave->master = NULL;

	if (i+1 < pty->n_slaves) {
		dst = pty->slaves[i];
		src = pty->slaves[i+1];
		len = (pty->n_slaves-i-1) * sizeof(struct pty *);
		memmove(dst, src, len);
	}
	pty->n_slaves--;
}

void
pty_free(struct pty *pty)
{
	while (pty->n_slaves)
		pty_remove_slave(pty, pty->slaves[pty->n_slaves-1]);

	if (pty->master != NULL)
		pty_remove_slave(pty->master, pty);

	if (pty->ptyfd != -1) {
		remove_event_source(pty->ptyfd);
		close(pty->ptyfd);
		pty->ptyfd = -1;
	}

	if (pty->cmd_editor != NULL)
		editor_free(pty->cmd_editor);

	if (pty->statbar != NULL)
		statbar_free(pty->statbar);
	if (pty->cwd != NULL)
		label_free(pty->cwd);
	if (pty->hbox != NULL)
		layout_free(pty->hbox);

	if (pty->cmd_buffer != NULL)
		buffer_free(pty->cmd_buffer);
	if (pty->cmd_cursor != NULL)
		buffer_cursor_free(pty->cmd_cursor);

	if (pty->ts_editor != NULL)
		editor_free(pty->ts_editor);

	if (pty->ts_buffer != NULL)
		buffer_free(pty->ts_buffer);
	if (pty->ts_icursor != NULL)
		buffer_cursor_free(pty->ts_icursor);
	if (pty->ts_ocursor != NULL)
		buffer_cursor_free(pty->ts_ocursor);

	if (pty->vbox != NULL)
		layout_free(pty->vbox);

	free(pty);
}
