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

#include "xevent.h"
#include "util.h"
#include "dpy.h"

#include <assert.h>
#include <string.h>

typedef enum handler_type {
	HANDLER_TYPE_KEYPRESS, HANDLER_TYPE_EXPOSE, HANDLER_TYPE_RESIZE,
	HANDLER_TYPE_TAKEFOCUS, HANDLER_TYPE_BUTTON
} HandlerType;

struct event_handler {
	HandlerType type;
	Window window;
	void *udata;
	union {
		KeypressHandler keypress;
		ExposeHandler expose;
		ResizeHandler resize;
		FocusHandler focus;
		ButtonHandler button;
	} v;
};

static struct event_handler *handlers;
static size_t n_handlers;
static size_t max_handlers;

static void	handle_xevent(XEvent *);
static void	run_xevent_handlers(XEvent *, HandlerType, Window);

extern struct dpy *dpy;

int
add_keypress_handler(Window window, KeypressHandler handler, void *udata)
{
	if (max_handlers == n_handlers)
		if (grow_array((void **) &handlers, sizeof(*handlers),
		    &max_handlers) == -1)
			return -1;

	handlers[n_handlers++] = (struct event_handler) {
		HANDLER_TYPE_KEYPRESS, window, udata, { .keypress = handler }
	};
	return 0;
}

int
add_button_handler(Window window, ButtonHandler handler, void *udata)
{
	if (max_handlers == n_handlers)
		if (grow_array((void **) &handlers, sizeof(*handlers),
		    &max_handlers) == -1)
			return -1;

	handlers[n_handlers++] = (struct event_handler) {
		HANDLER_TYPE_BUTTON, window, udata, { .button = handler }
	};
	return 0;
}

void
remove_handlers_for_window(Window window)
{
	int i;

	while (n_handlers > 0) {
		for (i = 0; i < n_handlers; i++)
			if (handlers[i].window == window)
				break;
		if (i == n_handlers)
			break;
		if (i+1 < n_handlers)
			memmove(&handlers[i], &handlers[i+1],
			    (n_handlers - i) * sizeof(struct event_handler));
		n_handlers--;
	}
}

int
add_expose_handler(Window window, ExposeHandler handler, void *udata)
{
	if (max_handlers == n_handlers)
		if (grow_array((void **) &handlers, sizeof(*handlers),
		    &max_handlers) == -1)
			return -1;

	handlers[n_handlers++] = (struct event_handler) {
		HANDLER_TYPE_EXPOSE, window, udata, { .expose = handler }
	};
	return 0;
}

int
add_focus_handler(Window window, FocusHandler handler, void *udata)
{
	if (max_handlers == n_handlers)
		if (grow_array((void **) &handlers, sizeof(*handlers),
		    &max_handlers) == -1)
			return -1;

	handlers[n_handlers++] = (struct event_handler) {
		HANDLER_TYPE_TAKEFOCUS, window, udata, { .focus = handler }
	};
	return 0;
}

int
add_resize_handler(Window window, ResizeHandler handler, void *udata)
{
	if (max_handlers == n_handlers)
		if (grow_array((void **) &handlers, sizeof(*handlers),
		    &max_handlers) == -1)
			return -1;

	handlers[n_handlers++] = (struct event_handler) {
		HANDLER_TYPE_RESIZE, window, udata, { .resize = handler }
	};
	return 0;
}

void
process_xevents(int fd, void *udata)
{
	XEvent event;

	while (XPending(DPY(dpy))) {
		XNextEvent(DPY(dpy), &event);
		handle_xevent(&event);
		XSync(DPY(dpy), False);
	}
}

static void
run_xevent_handlers(XEvent *event, HandlerType type, Window window)
{
	size_t i;

	for (i = 0; i < n_handlers; i++) {
		if (handlers[i].type != type || handlers[i].window != window)
			continue;

		switch (handlers[i].type) {
		case HANDLER_TYPE_BUTTON:
			handlers[i].v.button(
			    &event->xbutton, handlers[i].udata);
			break;
		case HANDLER_TYPE_KEYPRESS:
			handlers[i].v.keypress(
			    &event->xkey, handlers[i].udata);
			break;
		case HANDLER_TYPE_EXPOSE:
			handlers[i].v.expose(
			    &event->xexpose, handlers[i].udata);
			break;
		case HANDLER_TYPE_RESIZE:
			handlers[i].v.resize(
			    &event->xconfigure, handlers[i].udata);
			break;
		case HANDLER_TYPE_TAKEFOCUS:
			if (event->xclient.message_type ==
			    XInternAtom(DPY(dpy), "WM_PROTOCOLS", False) &&
			    event->xclient.format == 32 &&
			    event->xclient.data.l[0] == XInternAtom(DPY(dpy),
			    "WM_TAKE_FOCUS", False))
				handlers[i].v.focus(
				    event->xclient.data.l[1],
				    handlers[i].udata);
			break;
		default:
			assert(0);
		}
	}
}

static void
handle_xevent(XEvent *event)
{
	switch (event->type) {
	case Expose:
		/*
		 * TODO: Decide whether or not we want all expose events
		 *       or just the last one.
		 */
		if (1 || event->xexpose.count == 0) {
			run_xevent_handlers(event, HANDLER_TYPE_EXPOSE,
			    event->xexpose.window);
		}
		break;
	case ButtonPress:
		run_xevent_handlers(event, HANDLER_TYPE_BUTTON,
		    event->xbutton.window);
		break;
	case ConfigureNotify:
		run_xevent_handlers(event, HANDLER_TYPE_RESIZE,
		    event->xconfigure.window);
		break;
	case KeyPress:
		run_xevent_handlers(event, HANDLER_TYPE_KEYPRESS,
		    event->xkey.window);
		break;
	case ClientMessage:
		run_xevent_handlers(event, HANDLER_TYPE_TAKEFOCUS,
		    event->xclient.window);
		break;
	}
	return;
}
