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

#ifndef CONFIG_H
#define CONFIG_H

/*
 * WANT_OVERLAPPING_WINDOWS:
 *   For avoiding repaint flicker, sometimes it is better to have large
 *   windows moved partially beneath other windows rather than have small
 *   windows without overlap.
 */
/* #define WANT_OVERLAPPING_WINDOWS */

/*
 * WANT_FLUSHES_IN_REVERSE:
 *   For avoiding repaint flicker, sometimes it is good to flush updates
 *   in reverse order.
 */
/* #define WANT_FLUSHES_IN_REVERSE */

/*
 * WANT_LINE_NUMBERS:
 *   For debugging purposes, it is useful to have line numbers visible.
 *   Normally not always needed.
 */
#define WANT_LINE_NUMBERS

/*
 * DEBUG_UTF8:
 *   Print UTF-8 errors.
 */
/* #define DEBUG_UTF8 */

/*
 * CHUNK_BREAK_LIMIT:
 *   Break string drawing after certain number of bytes for avoiding
 *   drawing excessively over the window width. This is without needing
 *   to calculate size after each drawn character.
 */
#define CHUNK_BREAK_LIMIT 80

/*
 * ALLOC_CHUNK:
 *   How much bytes to allocate initially for dynamic arrays such as for
 *   lines.
 */
#define ALLOC_CHUNK 80

#endif
