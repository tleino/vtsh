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

#include "utf8.h"

#include <assert.h>

#ifdef DEBUG_UTF8
#include <err.h>
#endif

/*
 * Decreased offset in the UTF-8 string by _one_ cursor position.
 *
 * Returns the number decreased in offset.
 *
 * TODO: Optimize by reversing utf8_incr_col().
 */
int
utf8_decr_col(const char *s, size_t len, size_t *offset)
{
	size_t current, prev, begin;

	/*
	 * Not our responsibility to handle backtracking to the prev line.
	 */
	if (*offset == 0)
		return 0;

	prev = current = 0;
	begin = *offset;
	while (utf8_incr_col(s, len, &current, NULL) > 0) {
		if (current == *offset)
			break;
		prev = current;	
	}
	*offset = prev;

	assert(*offset < begin);
	return begin - *offset;
}

/*
 * Increase offset in the UTF-8 string by _one_ cursor position.
 *
 * Returns the number increased in offset.
 * 
 * If error is not NULL, sets error to 1 if we had a parse error,
 * otherwise to 0.
 */
int
utf8_incr_col(const char *s, size_t len, size_t *offset, int *error)
{
	size_t begin;
	int expect, overlong;
	unsigned char start, ch;

	if (error != NULL)
		*error = 0;

	/*
	 * Not our responsibility to handle advancing to the next line.
	 */
	if (*offset == len)
		return 0;
	assert(*offset < len);

	/*
	 * Store begin offset because we use it when falling back when
	 * encountering error conditions.
	 */
	begin = *offset;

	/*
	 * Step according to the start byte or single step in case of ASCII
	 * as well as error conditions.
	 */
	start = ch = (unsigned char) s[*offset];
	if (ch >= 0xF0 && ch <= 0xF4)
		expect = 4;
	else if (ch >= 0xE0 && ch <= 0xEF)
		expect = 3;
	else if (ch >= 0xC2 && ch <= 0xDF)
		expect = 2;
	else if (ch >= 0x00 && ch <= 0x7F)
		expect = 1;
	else {
#ifdef DEBUG_UTF8
		warnx("invalid UTF-8 start byte at %zu", *offset);
#endif
		expect = 1;
		if (error != NULL)
			*error = 1;
	}

	(*offset)++;
	expect--;
	overlong = 0;
	while (*offset < len && expect > 0) {
		ch = (unsigned char) s[*offset];
		/*
		 * Continuation. Check for exceptions according to
		 * Unicode14.0.0/ch03 Table3-7 specification.
		 */
		if (ch >= 0x80 && ch <= 0xBF) {
			overlong = 1;
			if (start == 0xE0 && expect == 2 && ch < 0xA0)
				break;
			else if (start == 0xED && expect == 2 && ch > 0x9F)
				break;
			else if (start == 0xF0 && expect == 3 && ch < 0x90)
				break;
			else if (start == 0xF4 && expect == 3 && ch > 0x8F)
				break;
			else
				overlong = 0;
		} else
			break;

		(*offset)++;
		expect--;
	}
	/*
	 * If we exited prematurely, we have error. Single step.
	 */
	if (expect > 0) {
#ifdef DEBUG_UTF8
		if (overlong)
			warnx("overlong UTF-8 sequence at %zu",
			    *offset);
		else
			warnx("premature end of UTF-8 sequence at %zu",
			    *offset);
#endif
		*offset = begin + 1;
		if (error != NULL)
			*error = 1;
	}

	return *offset - begin;
}
