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

/* font.c: Implements Xft(3) font and text drawing support. */

#include "font.h"
#include "color.h"
#include "dpy.h"

#include <X11/Xft/Xft.h>

#include <limits.h>
#include <assert.h>
#include <err.h>

#include "fontnames.c"

static XftColor	 fgcolor;
static XftColor	 bgcolor;
static XftFont	*ftfont[NUM_FONT];
static XftFont	*current_font;
static int	 space_width;
static XftDraw	*ftdraw;

extern struct dpy *dpy;

static XftFont	*font_load(int);
static void	 font_set_color(XftColor *, int);
static int	 _font_draw(Window, int, int, const char *, size_t);

#define TABWIDTH 8

static void
font_set_color(XftColor *ftcolor, int color)
{
	XColor xcolor;

	xcolor = query_color(dpy, color);

	ftcolor->pixel = xcolor.pixel;
	ftcolor->color.red = xcolor.red;
	ftcolor->color.green = xcolor.green;
	ftcolor->color.blue = xcolor.blue;
	ftcolor->color.alpha = USHRT_MAX;
}

/*
 * Sets font color by reusing named/enum-defined colors from color.c so
 * that we can use same color defines in font and non-font stuff.
 */
void
font_set_fgcolor(int color)
{
	font_set_color(&fgcolor, color);
}

void
font_set_bgcolor(int color)
{
	font_set_color(&bgcolor, color);
}

int
font_height()
{
	assert(current_font != NULL);

	return current_font->height;	
}

int
font_width()
{
	assert(current_font != NULL);

	return current_font->max_advance_width;
}

void
font_set(int id)
{
	XGlyphInfo extents;

	assert (id < NUM_FONT);

	if (ftfont[id] == NULL)
		ftfont[id] = font_load(id);

	current_font = ftfont[id];

	/*
	 * Check the width of single space. This is useful for setting
	 * the tab width.
	 */
	font_extents(" ", 1, &extents);
	space_width = extents.xOff;
}

void
font_extents(const char *text, size_t len, XGlyphInfo *extents)
{
	XftTextExtentsUtf8(DPY(dpy), current_font,
	    (const FcChar8 *) text, len, extents);
}

int
font_str_width(int x, const char *text, size_t len)
{
	XGlyphInfo extents;
	size_t i, j;
	int x_out;
	int tabwidth, tabstop, remaining;

	x_out = 0;
	j = 0;
	for (i = 0; i < len; i++) {
		if (text[i] == '\t') {
			if (j!=i) {
				font_extents(&text[j], i-j, &extents);
				x_out += extents.xOff;
			}
			j=i+1;

			tabwidth = space_width * TABWIDTH;
			tabstop = ((x+x_out) / tabwidth);
			remaining = tabwidth - ((x+x_out) -
			    (tabstop * tabwidth));

			x_out += remaining;
		}
	}
	if (j < len) {
		font_extents(&text[j], i-j, &extents);
		x_out += extents.xOff;
	}
	return x_out;
	
}

void
font_clear(Window window, int x, int y, int width)
{
	if (ftdraw == NULL)
		if ((ftdraw = XftDrawCreate(DPY(dpy), window,
		    DefaultVisual(DPY(dpy), DPY_SCREEN(dpy)),
		    DefaultColormap(DPY(dpy), DPY_SCREEN(dpy)))) ==
		    NULL)
			errx(1, "XftDrawCreate failed");

	if (XftDrawDrawable(ftdraw) != window)
		XftDrawChange(ftdraw, window);

	XftDrawRect(ftdraw, &bgcolor, x, y, width, current_font->height);
}

static int
_font_draw(Window window, int x, int y, const char *text, size_t len)
{
	XGlyphInfo extents;

	if (ftdraw == NULL)
		if ((ftdraw = XftDrawCreate(DPY(dpy), window,
		    DefaultVisual(DPY(dpy), DPY_SCREEN(dpy)),
		    DefaultColormap(DPY(dpy), DPY_SCREEN(dpy)))) ==
		    NULL)
			errx(1, "XftDrawCreate failed");

	if (XftDrawDrawable(ftdraw) != window)
		XftDrawChange(ftdraw, window);

	font_extents(text, len, &extents);

	XftDrawRect(ftdraw, &bgcolor, x, y, extents.xOff,
	    current_font->height);

	XftDrawStringUtf8(ftdraw, &fgcolor, current_font, x,
	    y + current_font->ascent, (const FcChar8 *) text, len);

	return extents.xOff;
}

int
font_draw(Window window, int x, int sx, int y, const char *text, size_t len)
{
	size_t i, j;
	int x_out, tabstop, tabwidth, remaining;

	x_out = 0;
	j = 0;
	for (i = 0; i < len; i++) {
		if (text[i] == '\t') {
			if (j!=i)
				x_out += _font_draw(window, sx+x_out, y,
				    &text[j], i-j);

			j=i+1;

			tabwidth = space_width * TABWIDTH;
			tabstop = ((x+x_out) / tabwidth);
			remaining = tabwidth - ((x+x_out) -
			    (tabstop * tabwidth));
			font_clear(window, sx+x_out, y, remaining);
			x_out += remaining;
		}
	}
	if (j < len)
		x_out += _font_draw(window, sx+x_out, y, &text[j], i-j);
	return x_out;
}

static XftFont *
font_load(int id)
{
	/*
	 * First try Xlfd form, then Xft font name form.
	 */
	ftfont[id] = XftFontOpenXlfd(DPY(dpy), DPY_SCREEN(dpy), fontname[id]);
	if (ftfont[id] == NULL)
		ftfont[id] = XftFontOpenName(DPY(dpy), DPY_SCREEN(dpy),
		    fontname[id]);

	/*
	 * No success? Try the fallback font.
	 */
	if (ftfont[id] == NULL && id != FONT_FALLBACK) {
		warnx("couldn't load font: %s", fontname[id]);
		return font_load(FONT_FALLBACK);
	} else if (ftfont[id] == NULL)
		errx(1, "couldn't load fallback font: %s", fontname[id]);

	return ftfont[id];
}

/*
 * TODO: Actually have separate ftdraw for each widget.
 * XftDrawChange() is nice, but causes problems.
 */
void
font_destroy_ftdraw()
{
	if (ftdraw != NULL) {
		XftDrawDestroy(ftdraw);
		ftdraw = NULL;
	}
}

void
font_close()
{
	int i;

	for (i = 0; i < NUM_FONT; i++) {
		if (ftfont[i] != NULL) {
			XftFontClose(DPY(dpy), ftfont[i]);
			ftfont[i] = NULL;
		}
	}

	font_destroy_ftdraw();
}
