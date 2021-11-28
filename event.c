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

#include "exvt.h"
#include "util.h"
#include "event.h"

#include <assert.h>
#include <sys/select.h>
#include <err.h>
#include <string.h>

struct event_source {
	int fd;
	void *udata;
	EventHandler handler;
};

static struct event_source *sources;
static size_t n_sources;
static size_t max_sources;

int
add_event_source(int fd, EventHandler handler, void *udata)
{
	if (max_sources == n_sources)
		if (grow_array((void **) &sources, sizeof(*sources),
		    &max_sources) == -1)
			return -1;

	sources[n_sources++] = (struct event_source) { fd, udata, handler };
	return 0;
}

void
remove_event_source(int fd)
{
	size_t i;

	for (i = 0; i < n_sources; i++)
		if (sources[i].fd == fd)
			break;

	if (i == n_sources)
		return;

	n_sources--;
	if (n_sources > 0 && i < n_sources-1)
		memmove(&sources[i], &sources[i+1], (n_sources - i) *
		    sizeof(struct event_source));
}

void
run_event_loop()
{
	fd_set rfds;
	size_t nready, i, maxfd;

	FD_ZERO(&rfds);

	maxfd = 0;
	for (i = 0; i < n_sources; i++) {
		FD_SET(sources[i].fd, &rfds);
		maxfd = MAX(sources[i].fd, maxfd);
	}

	nready = select(maxfd + 1, &rfds, NULL, NULL, NULL);
	if (nready == -1 || nready == 0)
		err(1, "select");

	while (nready)
		for (i = 0; i < n_sources; i++)
			if (FD_ISSET(sources[i].fd, &rfds)) {
				sources[i].handler(sources[i].fd,
				    sources[i].udata);
				nready--;
				break;
			}

	sources[0].handler(sources[0].fd, sources[0].udata);
}

#ifdef TEST
#include <unistd.h>

void read_stdin(int fd, void *udata)
{
	char buf[256];
	int n;

	n = read(fd, buf, sizeof(buf));
	if (n <= 0) {
		warn("got eof or error");
		return;
	}

	buf[n] = '\0';
	warnx("got %s", buf);
}

int
main(int argc, char **argv)
{
	if (add_event_source(0, read_stdin, NULL) == -1)
		err(1, "add_event_source");

	for (;;)
		run_event_loop();

	return 0;
}
#endif
