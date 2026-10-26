/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

#define UTF_INVALID 0xFFFD

static int
utf8decode(const char *s_in, long *u, int *err)
{
	static const unsigned char lens[] = {
		/* 0XXXX */ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		/* 10XXX */ 0, 0, 0, 0, 0, 0, 0, 0,  /* invalid */
		/* 110XX */ 2, 2, 2, 2,
		/* 1110X */ 3, 3,
		/* 11110 */ 4,
		/* 11111 */ 0,  /* invalid */
	};
	static const unsigned char leading_mask[] = { 0x7F, 0x1F, 0x0F, 0x07 };
	static const unsigned int overlong[] = { 0x0, 0x80, 0x0800, 0x10000 };

	const unsigned char *s = (const unsigned char *)s_in;
	int len = lens[*s >> 3];
	*u = UTF_INVALID;
	*err = 1;
	if (len == 0)
		return 1;

	long cp = s[0] & leading_mask[len - 1];
	for (int i = 1; i < len; ++i) {
		if (s[i] == '\0' || (s[i] & 0xC0) != 0x80)
			return i;
		cp = (cp << 6) | (s[i] & 0x3F);
	}
	/* out of range, surrogate, overlong encoding */
	if (cp > 0x10FFFF || (cp >> 11) == 0x1B || cp < overlong[len - 1])
		return len;

	*err = 0;
	*u = cp;
	return len;
}

Drw *
drw_create(Display *dpy, int screen, Window root, unsigned int w, unsigned int h)
{
	Drw *drw = ecalloc(1, sizeof(Drw));

	drw->dpy = dpy;
	drw->screen = screen;
	drw->root = root;
	drw->w = w;
	drw->h = h;
	drw->drawable = XCreatePixmap(dpy, root, w, h, DefaultDepth(dpy, screen));
	drw->gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, drw->gc, 1, LineSolid, CapButt, JoinMiter);

	return drw;
}

void
drw_resize(Drw *drw, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	drw->w = w;
	drw->h = h;
	if (drw->drawable)
		XFreePixmap(drw->dpy, drw->drawable);
	drw->drawable = XCreatePixmap(drw->dpy, drw->root, w, h, DefaultDepth(drw->dpy, drw->screen));
}

void
drw_free(Drw *drw)
{
	XFreePixmap(drw->dpy, drw->drawable);
	XFreeGC(drw->dpy, drw->gc);
	drw_fontset_free(drw->fonts);
	free(drw);
}

/* This function is an implementation detail. Library users should use
 * drw_fontset_create instead.
 */
static Fnt *
xfont_create(Drw *drw, const char *fontname, FcPattern *fontpattern)
{
	Fnt *font;
	XftFont *xfont = NULL;
	FcPattern *pattern = NULL;

	if (fontname) {
		/* Using the pattern found at font->xfont->pattern does not yield the
		 * same substitution results as using the pattern returned by
		 * FcNameParse; using the latter results in the desired fallback
		 * behaviour whereas the former just results in missing-character
		 * rectangles being drawn, at least with some fonts. */
		if (!(xfont = XftFontOpenName(drw->dpy, drw->screen, fontname))) {
			fprintf(stderr, "error, cannot load font from name: '%s'\n", fontname);
			return NULL;
		}
		if (!(pattern = FcNameParse((FcChar8 *) fontname))) {
			fprintf(stderr, "error, cannot parse font name to pattern: '%s'\n", fontname);
			XftFontClose(drw->dpy, xfont);
			return NULL;
		}
	} else if (fontpattern) {
		if (!(xfont = XftFontOpenPattern(drw->dpy, fontpattern))) {
			fprintf(stderr, "error, cannot load font from pattern.\n");
			return NULL;
		}
	} else {
		die("no font specified.");
	}

	font = ecalloc(1, sizeof(Fnt));
	font->xfont = xfont;
	font->pattern = pattern;
	font->h = xfont->ascent + xfont->descent;
	font->dpy = drw->dpy;

	return font;
}

static void
xfont_free(Fnt *font)
{
	if (!font)
		return;
	if (font->pattern)
		FcPatternDestroy(font->pattern);
	XftFontClose(font->dpy, font->xfont);
	free(font);
}

Fnt*
drw_fontset_create(Drw* drw, const char *fonts[], size_t fontcount)
{
	Fnt *cur, *ret = NULL;
	size_t i;

	if (!drw || !fonts)
		return NULL;

	for (i = 1; i <= fontcount; i++) {
		if ((cur = xfont_create(drw, fonts[fontcount - i], NULL))) {
			cur->next = ret;
			ret = cur;
		}
	}
	return (drw->fonts = ret);
}

void
drw_fontset_free(Fnt *font)
{
	if (font) {
		drw_fontset_free(font->next);
		xfont_free(font);
	}
}

void
drw_clr_create(Drw *drw, Clr *dest, const char *clrname)
{
	if (!drw || !dest || !clrname)
		return;

	if (!XftColorAllocName(drw->dpy, DefaultVisual(drw->dpy, drw->screen),
	                       DefaultColormap(drw->dpy, drw->screen),
	                       clrname, dest))
		die("error, cannot allocate color '%s'", clrname);
}

/* Create color schemes. */
Clr *
drw_scm_create(Drw *drw, const char *clrnames[], size_t clrcount)
{
	size_t i;
	Clr *ret;

	/* need at least two colors for a scheme */
	if (!drw || !clrnames || clrcount < 2 || !(ret = ecalloc(clrcount, sizeof(Clr))))
		return NULL;

	for (i = 0; i < clrcount; i++)
		drw_clr_create(drw, &ret[i], clrnames[i]);
	return ret;
}

void
drw_clr_free(Drw *drw, Clr *c)
{
	if (!drw || !c)
		return;

	/* c is typedef XftColor Clr */
	XftColorFree(drw->dpy, DefaultVisual(drw->dpy, drw->screen),
	             DefaultColormap(drw->dpy, drw->screen), c);
}

void
drw_scm_free(Drw *drw, Clr *scm, size_t clrcount)
{
	size_t i;

	if (!drw || !scm)
		return;

	for (i = 0; i < clrcount; i++)
		drw_clr_free(drw, &scm[i]);
	free(scm);
}

void
drw_setfontset(Drw *drw, Fnt *set)
{
	if (drw)
		drw->fonts = set;
}

void
drw_setscheme(Drw *drw, Clr *scm)
{
	if (drw)
		drw->scheme = scm;
}

void
drw_rect(Drw *drw, int x, int y, unsigned int w, unsigned int h, int filled, int invert)
{
	if (!drw || !drw->scheme)
		return;
	XSetForeground(drw->dpy, drw->gc, invert ? drw->scheme[ColBg].pixel : drw->scheme[ColFg].pixel);
	if (filled)
		XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
	else
		XDrawRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w - 1, h - 1);
}

int
drw_text(Drw *drw, int x, int y, unsigned int w, unsigned int h, unsigned int lpad, const char *text, int invert)
{
	int ty, ellipsis_x = 0;
	unsigned int tmpw, ew, ellipsis_w = 0, ellipsis_len, hash, h0, h1;
	XftDraw *d = NULL;
	Fnt *usedfont, *curfont, *nextfont;
	int utf8strlen, utf8charlen, utf8err, render = x || y || w || h;
	long utf8codepoint = 0;
	const char *utf8str;
	FcCharSet *fccharset;
	FcPattern *fcpattern;
	FcPattern *match;
	XftResult result;
	int charexists = 0, overflow = 0;
	/* keep track of a couple codepoints for which we have no match. */
	static unsigned int nomatches[128], ellipsis_width, invalid_width;
	static const char invalid[] = "�";

	if (!drw || (render && (!drw->scheme || !w)) || !text || !drw->fonts)
		return 0;

	if (!render) {
		w = invert ? invert : ~invert;
	} else {
		if (!(invert & 2)) {
			XSetForeground(drw->dpy, drw->gc, drw->scheme[invert & 1 ? ColFg : ColBg].pixel);
			XFillRectangle(drw->dpy, drw->drawable, drw->gc, x, y, w, h);
		}
		if (w < lpad)
			return x + w;
		d = XftDrawCreate(drw->dpy, drw->drawable,
		                  DefaultVisual(drw->dpy, drw->screen),
		                  DefaultColormap(drw->dpy, drw->screen));
		x += lpad;
		w -= lpad;
	}

	usedfont = drw->fonts;
	if (!ellipsis_width && render)
		ellipsis_width = drw_fontset_getwidth(drw, "...");
	if (!invalid_width && render)
		invalid_width = drw_fontset_getwidth(drw, invalid);
	while (1) {
		ew = ellipsis_len = utf8err = utf8charlen = utf8strlen = 0;
		utf8str = text;
		nextfont = NULL;
		while (*text) {
			utf8charlen = utf8decode(text, &utf8codepoint, &utf8err);
			for (curfont = drw->fonts; curfont; curfont = curfont->next) {
				charexists = charexists || XftCharExists(drw->dpy, curfont->xfont, utf8codepoint);
				if (charexists) {
					drw_font_getexts(curfont, text, utf8charlen, &tmpw, NULL);
					if (ew + ellipsis_width <= w) {
						/* keep track where the ellipsis still fits */
						ellipsis_x = x + ew;
						ellipsis_w = w - ew;
						ellipsis_len = utf8strlen;
					}

					if (ew + tmpw > w) {
						overflow = 1;
						/* called from drw_fontset_getwidth_clamp():
						 * it wants the width AFTER the overflow
						 */
						if (!render)
							x += tmpw;
						else
							utf8strlen = ellipsis_len;
					} else if (curfont == usedfont) {
						text += utf8charlen;
						utf8strlen += utf8err ? 0 : utf8charlen;
						ew += utf8err ? 0 : tmpw;
					} else {
						nextfont = curfont;
					}
					break;
				}
			}

			if (overflow || !charexists || nextfont || utf8err)
				break;
			else
				charexists = 0;
		}

		if (utf8strlen) {
			if (render) {
				ty = y + (h - usedfont->h) / 2 + usedfont->xfont->ascent;
				XftDrawStringUtf8(d, &drw->scheme[invert & 1 ? ColBg : ColFg],
				                  usedfont->xfont, x, ty, (XftChar8 *)utf8str, utf8strlen);
			}
			x += ew;
			w -= ew;
		}
		if (utf8err && (!render || invalid_width < w)) {
			if (render)
				drw_text(drw, x, y, w, h, 0, invalid, invert);
			x += invalid_width;
			w -= invalid_width;
		}
		if (render && overflow)
			drw_text(drw, ellipsis_x, y, ellipsis_w, h, 0, "...", invert);

		if (!*text || overflow) {
			break;
		} else if (nextfont) {
			charexists = 0;
			usedfont = nextfont;
		} else {
			/* Regardless of whether or not a fallback font is found, the
			 * character must be drawn. */
			charexists = 1;

			hash = (unsigned int)utf8codepoint;
			hash = ((hash >> 16) ^ hash) * 0x21F0AAAD;
			hash = ((hash >> 15) ^ hash) * 0xD35A2D97;
			h0 = ((hash >> 15) ^ hash) % LENGTH(nomatches);
			h1 = (hash >> 17) % LENGTH(nomatches);
			/* avoid expensive XftFontMatch call when we know we won't find a match */
			if (nomatches[h0] == utf8codepoint || nomatches[h1] == utf8codepoint)
				goto no_match;

			fccharset = FcCharSetCreate();
			FcCharSetAddChar(fccharset, utf8codepoint);

			if (!drw->fonts->pattern) {
				/* Refer to the comment in xfont_create for more information. */
				die("the first font in the cache must be loaded from a font string.");
			}

			fcpattern = FcPatternDuplicate(drw->fonts->pattern);
			FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
			FcPatternAddBool(fcpattern, FC_SCALABLE, FcTrue);

			FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
			FcDefaultSubstitute(fcpattern);
			match = XftFontMatch(drw->dpy, drw->screen, fcpattern, &result);

			FcCharSetDestroy(fccharset);
			FcPatternDestroy(fcpattern);

			if (match) {
				usedfont = xfont_create(drw, NULL, match);
				if (usedfont && XftCharExists(drw->dpy, usedfont->xfont, utf8codepoint)) {
					for (curfont = drw->fonts; curfont->next; curfont = curfont->next)
						; /* NOP */
					curfont->next = usedfont;
				} else {
					xfont_free(usedfont);
					nomatches[nomatches[h0] ? h1 : h0] = utf8codepoint;
no_match:
					usedfont = drw->fonts;
				}
			}
		}
	}
	if (d)
		XftDrawDestroy(d);

	return x + (render ? w : 0);
}

DrwIcon *
drw_icon_create(const unsigned long *data, unsigned long count, unsigned int target)
{
	unsigned long i = 0, best = 0, pixels, w, h, size, chosen = 0;
	unsigned int x, y;
	DrwIcon *icon;

	if (!data || !count || count > 262146 || !target || target > 16)
		return NULL;
	while (i < count) {
		if (count - i < 2)
			return NULL;
		w = data[i++];
		h = data[i++];
		if (!w || !h || w > 512 || h > 512)
			return NULL;
		pixels = w * h; /* Both operands are bounded before multiplication. */
		if (pixels > count - i)
			return NULL;
		size = MAX(w, h);
		if (!chosen || (size >= target && (chosen < target || size < chosen))
		|| (size < target && chosen < target && size > chosen)) {
			best = i - 2;
			chosen = size;
		}
		i += pixels;
	}
	w = data[best];
	h = data[best + 1];
	if (!(icon = calloc(1, sizeof *icon)))
		return NULL;
	icon->w = MAX(1, w * target / chosen);
	icon->h = MAX(1, h * target / chosen);
	if (!(icon->pixels = calloc(icon->w * icon->h, sizeof *icon->pixels))) {
		free(icon);
		return NULL;
	}
	for (y = 0; y < icon->h; y++)
		for (x = 0; x < icon->w; x++)
			icon->pixels[y * icon->w + x] = data[best + 2
				+ (y * h / icon->h) * w + x * w / icon->w] & 0xffffffffUL;
	return icon;
}

void
drw_icon_free(DrwIcon *icon)
{
	if (!icon)
		return;
	free(icon->pixels);
	free(icon);
}

int
drw_icon(Drw *drw, int x, int y, const DrwIcon *icon, int invert)
{
	Visual *visual;
	XImage *image;
	unsigned long masks[3], maxima[3], bg, pixel, component;
	unsigned int shifts[3], i, xx, yy, argb, alpha, src, back;

	if (!drw || !drw->scheme || !icon || !icon->pixels)
		return 0;
	visual = DefaultVisual(drw->dpy, drw->screen);
	if (!visual || visual->class != TrueColor)
		return 0;
	masks[0] = visual->red_mask;
	masks[1] = visual->green_mask;
	masks[2] = visual->blue_mask;
	if ((masks[0] & masks[1]) || (masks[0] & masks[2]) || (masks[1] & masks[2]))
		return 0;
	for (i = 0; i < 3; i++) {
		if (!masks[i] || masks[i] > 0xffffffffUL)
			return 0;
		for (shifts[i] = 0; !((masks[i] >> shifts[i]) & 1); shifts[i]++)
			;
		maxima[i] = masks[i] >> shifts[i];
		if ((maxima[i] & (maxima[i] + 1)) || maxima[i] > 65535)
			return 0;
	}
	image = XCreateImage(drw->dpy, visual, DefaultDepth(drw->dpy, drw->screen),
		ZPixmap, 0, NULL, icon->w, icon->h, 32, 0);
	if (!image)
		return 0;
	if (!(image->data = calloc(icon->h, image->bytes_per_line))) {
		XDestroyImage(image);
		return 0;
	}
	bg = drw->scheme[invert ? ColFg : ColBg].pixel;
	for (yy = 0; yy < icon->h; yy++)
		for (xx = 0; xx < icon->w; xx++) {
			argb = icon->pixels[yy * icon->w + xx];
			alpha = argb >> 24;
			pixel = bg & ~(masks[0] | masks[1] | masks[2]);
			for (i = 0; i < 3; i++) {
				src = (argb >> (16 - 8 * i)) & 255;
				back = ((bg & masks[i]) >> shifts[i]);
				component = (src * maxima[i] * alpha / 255
					+ back * (255 - alpha) + 127) / 255;
				pixel |= component << shifts[i];
			}
			XPutPixel(image, xx, yy, pixel);
		}
	XPutImage(drw->dpy, drw->drawable, drw->gc, image, 0, 0, x, y, icon->w, icon->h);
	XDestroyImage(image);
	return 1;
}

void
drw_map(Drw *drw, Window win, int x, int y, unsigned int w, unsigned int h)
{
	if (!drw)
		return;

	XCopyArea(drw->dpy, drw->drawable, win, drw->gc, x, y, w, h, x, y);
	XSync(drw->dpy, False);
}

unsigned int
drw_fontset_getwidth(Drw *drw, const char *text)
{
	if (!drw || !drw->fonts || !text)
		return 0;
	return drw_text(drw, 0, 0, 0, 0, 0, text, 0);
}

unsigned int
drw_fontset_getwidth_clamp(Drw *drw, const char *text, unsigned int n)
{
	unsigned int tmp = 0;
	if (drw && drw->fonts && text && n)
		tmp = drw_text(drw, 0, 0, 0, 0, 0, text, n);
	return MIN(n, tmp);
}

void
drw_font_getexts(Fnt *font, const char *text, unsigned int len, unsigned int *w, unsigned int *h)
{
	XGlyphInfo ext;

	if (!font || !text)
		return;

	XftTextExtentsUtf8(font->dpy, font->xfont, (XftChar8 *)text, len, &ext);
	if (w)
		*w = ext.xOff;
	if (h)
		*h = font->h;
}

Cur *
drw_cur_create(Drw *drw, int shape)
{
	Cur *cur;

	if (!drw || !(cur = ecalloc(1, sizeof(Cur))))
		return NULL;

	cur->cursor = XCreateFontCursor(drw->dpy, shape);

	return cur;
}

void
drw_cur_free(Drw *drw, Cur *cursor)
{
	if (!cursor)
		return;

	XFreeCursor(drw->dpy, cursor->cursor);
	free(cursor);
}
