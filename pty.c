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
#include "button.h"

#ifdef HAVE_PTY_H
#include <pty.h>
#else
#include <util.h>
#endif

#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <termios.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>

struct editor;

static int	pty_create_cmd(struct pty *);
static int	pty_create_ts(struct pty *);
static void	pty_recreate_ts_buffer(struct pty *);
static void	pty_submit_command(const char *, void *);
static void	pty_submit_stdin(const char *, void *);
static void	pty_process_events(int, void *);

static int	pty_add_slave(struct pty *, struct pty *);
static int	pty_find_slave(struct pty *, struct pty *);
static void	pty_remove_slave(struct pty *, struct pty *);

static void	pty_file_updated(int, int, int, int, BufferUpdate, void *);
static void	pty_exec_handler(const char *, void *);

static void	pty_action(struct pty *, PtyAction, const char *);

static void	pty_close_button(struct button *, void *);
static void	pty_hide_button(struct button *, void *);

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

	pty->hide_button = button_create("[H]", pty_hide_button, pty,
	    "hide_button", WIDGET(pty->hbox));
	pty->close_button = button_create("[X]", pty_close_button, pty,
	    "close_button", WIDGET(pty->hbox));

	statbar_update_status(pty->statbar, STATBAR_STATE_NOT_STARTED, 0, 0, 0);
	return pty;

fail:
	pty_free(pty);
	return NULL;
}

void
pty_set_action_callback(struct pty *pty, PtyActionCallback ptyaction,
    void *udata)
{
	pty->ptyaction = ptyaction;
	pty->ptyaction_udata = udata;
}

static void
pty_close_button(struct button *button, void *udata)
{
	struct pty *pty = udata;

	pty_action(pty, PtyActionClose, "");
}

static void
pty_hide_button(struct button *button, void *udata)
{
	struct pty *pty = udata;

	pty_action(pty, PtyActionToggleHide, "");
}

static void
pty_action(struct pty *pty, PtyAction action, const char *s)
{
	if (pty->ptyaction == NULL)
		return;

	pty->ptyaction(pty, action, s, pty->ptyaction_udata);
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

void
pty_show_output(struct pty *pty)
{
	if (pty->ts_editor) {
		widget_show(WIDGET(pty->ts_editor));
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

			editor_shrink(pty->ts_editor);
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
		pty->ts_icursor->offset = 0;
		pty->ts_ocursor->row = pty->ts_icursor->row;
		pty->ts_ocursor->offset = pty->ts_icursor->offset;

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
pty_file_updated(int x, int y, int w, int h, BufferUpdate type, void *udata)
{
	struct pty *pty = udata;

	if (pty->file_unsaved)
		return;

	statbar_update_status(pty->statbar, STATBAR_STATE_FILE_UNSAVED,
	    0, 0, buffer_rows(pty->ts_buffer));
	pty->file_unsaved = 1;
}

void
pty_save(struct pty *pty)
{
	int i, rows;
	const char *p;
	size_t sz;

	if (pty->ts_buffer == NULL || pty->file == NULL)
		return;

	if (pty->fp != NULL)
		fclose(pty->fp);

	pty->fp = fopen(pty->file, "w");
	if (pty->fp == NULL) {
		warn("%s", pty->file);
		return;
	}

	rows = buffer_rows(pty->ts_buffer);
	for (i = 0; rows > 0 && i < rows-1; i++) {
		p = buffer_u8str_at(pty->ts_buffer, i, &sz);
		if (p != NULL) {
			fwrite(p, sz, 1, pty->fp);
			fwrite("\n", 1, 1, pty->fp);
		}
	}
	fclose(pty->fp);

	if (pty->file_unsaved)
		statbar_update_status(pty->statbar, STATBAR_STATE_FILE_SAVED,
		    0, 0, buffer_rows(pty->ts_buffer));
	pty->file_unsaved = 0;
}

static void
pty_exec_handler(const char *s, void *udata)
{
	struct pty *pty = udata;

	pty_action(pty, PtyActionOpen, s);
}

void
pty_run_command(struct pty *pty, const char *s)
{
	buffer_clear_row(pty->cmd_buffer, 0);
	pty->cmd_cursor->offset = 0;
	buffer_insert(pty->cmd_cursor, s, strlen(s));
	pty_submit_command(s, pty);
}

static void
pty_submit_command(const char *s, void *udata)
{
	int status;
	char *sh;
	const char *p;
	struct pty *pty = udata, *master;
	struct termios ts;
	size_t len;
	int i, send_ts, use_file, use_dir;
	size_t n;
	char buf[4096];
	char *delim = "\x04";
	struct dirent *ent;
	char resolved[PATH_MAX + 1];
	struct stat sb;

	len = strlen(s);
	send_ts = 0;
	use_file = 0;
	use_dir = 0;
	if (len >= 2 && s[len-2] == '<' && s[len-1] == '.') {
		send_ts = 1;
		len -= 2;
		delim = ".\n";
	} else if (len >= 1 && s[len-1] == '<') {
		send_ts = 1;
		len--;
	}

	if (send_ts == 0 && s[0] == ':' && len > 1) {
		s++;
		len--;
		if (len > 0 && s[len-1] == '/')
			use_dir = 1;
		else
			use_file = 1;

		if (pty->fp != NULL)
			fclose(pty->fp);
		if (pty->file != NULL) {
			free(pty->file);
			pty->file = NULL;
		}

		if (use_file) {
			pty->fp = fopen(s, "r");
			pty->file = strdup(s);
		} else {
			if (realpath(s, resolved) != NULL) {
				pty->dp = opendir(resolved);
				if (pty->dp != NULL) {
					if (fchdir(dirfd(pty->dp)) != -1) {
						label_set(pty->cwd, resolved);
						pty->cmd_cursor->offset = 0;
						buffer_clear_row(
						    pty->cmd_buffer, 0);
						buffer_insert(
						    pty->cmd_cursor, ":./", 3);
					} else
						warn("fchdir %s", resolved);
				}
			}
		}
	}

	master = pty->master;
	if (master != NULL) {
		master->active_slave = pty;

		if (!send_ts || len > 0) {
			write(master->ptyfd, s, len);
			write(master->ptyfd, "\n", 1);
		}

		if (send_ts) {
			for (i = 0; i < buffer_rows(pty->ts_buffer); i++) {
				p = buffer_u8str_at(pty->ts_buffer, i, &n);
				if (p != NULL) {
					write(master->ptyfd, p, n);
					write(master->ptyfd, "\n", 1);
				}
			}
			write(master->ptyfd, delim, strlen(delim));
		}

		if (pty->ts_buffer != NULL)
			pty_recreate_ts_buffer(pty);
		pty_show_output(pty);
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
	if (pty->ts_buffer != NULL)
		pty_recreate_ts_buffer(pty);

	if (use_file) {
		if (pty->fp == NULL && errno == ENOENT) {
			/* TODO: Indicate this is a new file */
		} else if (pty->fp == NULL) {
			buffer_insert(pty->ts_ocursor, strerror(errno),
			    strlen(strerror(errno)));
		} else {
			while ((n = fread(buf, sizeof(char), sizeof(buf),
			    pty->fp)) > 0)
				buffer_insert(pty->ts_ocursor, buf, n);
		}
		statbar_update_status(pty->statbar, STATBAR_STATE_FILE_SAVED,
		    0, 0, buffer_rows(pty->ts_buffer));

		buffer_add_listener(pty->ts_buffer, pty_file_updated, pty);

		pty_show_output(pty);
		return;
	} else if (use_dir) {
		if (pty->dp == NULL) {
			buffer_insert(pty->ts_ocursor, strerror(errno),
			    strlen(strerror(errno)));
		} else {
			while ((ent = readdir(pty->dp)) != NULL) {
				buffer_insert(pty->ts_ocursor, ":", 1);
				buffer_insert(pty->ts_ocursor, ent->d_name,
				    ent->d_namlen);
				if (lstat(ent->d_name, &sb) == -1)
					warn("fstat %s", ent->d_name);
				else {
					if (S_ISDIR(sb.st_mode))
						buffer_insert(pty->ts_ocursor,
						    "/\n", 2);
					else
						buffer_insert(pty->ts_ocursor,
						    "\n", 1);
				}
			}
			closedir(pty->dp);
			pty->dp = NULL;
		}
		pty_show_output(pty);
		return;
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
	ts.c_cc[VEOF] = 0x04;
	ts.c_cc[VINTR] = 0x03;
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

	pty_show_output(pty);
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
	    pty_submit_command, pty, COLOR_TITLE_BG_NORMAL, 1, 1,
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
	    pty_submit_stdin, pty, COLOR_TEXT_BG, -1, 0, "ts_editor",
	    pty->widget)) == NULL)
		return -1;

	pty->ts_editor->exec = pty_exec_handler;
	pty->ts_editor->exec_udata = pty;

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

static void
pty_recreate_ts_buffer(struct pty *pty)
{
	buffer_cursor_free(pty->ts_icursor);
	buffer_cursor_free(pty->ts_ocursor);
	buffer_free(pty->ts_buffer);
	if ((pty->ts_buffer = buffer_create()) == NULL)
		err(1, "buffer");
	if ((pty->ts_icursor = buffer_cursor_create(pty->ts_buffer)) == NULL)
		err(1, "input_cursor");
	if ((pty->ts_ocursor = buffer_cursor_create(pty->ts_buffer)) == NULL)
		err(1, "output_cursor");

	editor_set_cursor(pty->ts_editor, pty->ts_icursor, pty->ts_ocursor);
	pty->ts_editor->old_height = 0;	/* TODO: Make editor do this */
}

void
pty_free(struct pty *pty)
{
	while (pty->n_slaves)
		pty_remove_slave(pty, pty->slaves[pty->n_slaves-1]);
	if (pty->slaves != NULL)
		free(pty->slaves);

	if (pty->fp != NULL)
		fclose(pty->fp);

	if (pty->file != NULL)
		free(pty->file);

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
	if (pty->close_button != NULL)
		button_free(pty->close_button);
	if (pty->hide_button != NULL)
		button_free(pty->hide_button);
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
