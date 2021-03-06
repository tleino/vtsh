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

#ifndef XEVENT_H
#define XEVENT_H

#include <X11/Xlib.h>

typedef void (*KeypressHandler)(XKeyEvent *, void *);
typedef void (*ButtonHandler)(XButtonEvent *, void *);
typedef void (*MotionHandler)(XMotionEvent *, void *);
typedef void (*ExposeHandler)(XExposeEvent *, void *);
typedef void (*ResizeHandler)(XConfigureEvent *, void *);
typedef void (*FocusHandler)(Time, void *);
typedef void (*DestroyHandler)(void *);

void	process_xevents(int, void *);
int	add_keypress_handler(Window, KeypressHandler, void *);
int	add_button_handler(Window, ButtonHandler, void *);
int	add_motion_handler(Window, MotionHandler, void *);
int	add_expose_handler(Window, ExposeHandler, void *);
int	add_resize_handler(Window, ResizeHandler, void *);
int	add_focus_handler(Window, FocusHandler, void *);
int	add_destroy_handler(Window, DestroyHandler, void *);

int	have_xevents(void);

void	remove_handlers_for_window(Window);

#endif
