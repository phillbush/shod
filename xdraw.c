#include <err.h>
#include <stdlib.h>

#include "shod.h"

static GC gc;
static struct Theme {
	XftFont *font;
	XftColor colors[STYLE_LAST][COLOR_LAST];
} theme;

static int
alloccolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color)) {
		warnx("could not allocate color: %s", s);
		return 0;
	}
	return 1;
}

static XftFont *
openfont(const char *s)
{
	XftFont *font = NULL;

	if ((font = XftFontOpenXlfd(dpy, screen, s)) == NULL)
		if ((font = XftFontOpenName(dpy, screen, s)) == NULL)
			warnx("could not open font: %s", s);
	return font;
}

void
pixmapnew(Pixmap *pix, Window win, int w, int h)
{
	if (*pix != None)
		XFreePixmap(dpy, *pix);
	*pix = XCreatePixmap(dpy, win, w, h, depth);
}

void
drawrectangle(Pixmap pix, int x, int y, int w, int h, unsigned long color)
{
	XChangeGC(
		dpy, gc,
		GCForeground,
		&(XGCValues){ .foreground = color }
	);
	XFillRectangle(dpy, pix, gc, x, y, w, h);
}

void
drawcommit(Pixmap pix, Window win)
{
	XSetWindowBackgroundPixmap(dpy, win, pix);
	XClearWindow(dpy, win);
}

/* draw text into drawable */
void
drawtitle(Drawable pix, const char *text, int w, int drawlines, int style, int pressed, int ismenu)
{
	XGCValues val;
	XGlyphInfo box;
	XftColor *color;
	XftDraw *draw;
	size_t len;
	unsigned int top, bot;
	int i, x, y;

	top = theme.colors[style][pressed ? COLOR_DARK : COLOR_LIGHT].pixel;
	bot = theme.colors[style][pressed ? COLOR_LIGHT : COLOR_DARK].pixel;
	if (ismenu || drawlines)
		color = &theme.colors[STYLE_OTHER][COLOR_FG];
	else
		color = &theme.colors[style][COLOR_LIGHT];
	draw = XftDrawCreate(dpy, pix, visual, colormap);
	len = strlen(text);
	XftTextExtentsUtf8(dpy, theme.font, (FcChar8 *)text, len, &box);
	x = max(0, (w - box.width) / 2 + box.x);
	y = (config.titlewidth - theme.font->ascent) / 2 + theme.font->ascent;
	for (i = 3; drawlines && i < config.titlewidth - 3; i += 3) {
		val.foreground = top;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangle(dpy, pix, gc, 4, i, x - 8, 1);
		XFillRectangle(dpy, pix, gc, w - x + 2, i, x - 6, 1);
	}
	for (i = 4; drawlines && i < config.titlewidth - 2; i += 3) {
		val.foreground = bot;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangle(dpy, pix, gc, 4, i, x - 8, 1);
		XFillRectangle(dpy, pix, gc, w - x + 2, i, x - 6, 1);
	}
	XftDrawStringUtf8(draw, color, theme.font, x, y, (FcChar8 *)text, len);
	XftDrawDestroy(draw);
}

/* draw borders with shadows */
void
drawborders(Pixmap pix, int w, int h, int style)
{
	XGCValues val;
	XRectangle *recs;
	XftColor *decor;
	int partw, parth;
	int i;

	if (w <= 0 || h <= 0)
		return;

	decor = theme.colors[style];
	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	recs = ecalloc(config.shadowthickness * 4, sizeof(*recs));

	/* draw background */
	val.foreground = decor[COLOR_MID].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, w, h);

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 4 + 0] = (XRectangle){.x = i, .y = i, .width = 1, .height = h - 1 - i};
		recs[i * 4 + 1] = (XRectangle){.x = i, .y = i, .width = w - 1 - i, .height = 1};
		recs[i * 4 + 2] = (XRectangle){.x = w - config.borderwidth + i, .y = config.borderwidth - 1 - i, .width = 1, .height = parth + 2 * (i + 1)};
		recs[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 * (i + 1), .height = 1};
	}
	val.foreground = decor[COLOR_LIGHT].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 4 + 0] = (XRectangle){.x = w - 1 - i, .y = i,         .width = 1,         .height = h - i * 2};
		recs[i * 4 + 1] = (XRectangle){.x = i,         .y = h - 1 - i, .width = w - i * 2, .height = 1};
		recs[i * 4 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = 1,                 .height = parth + 1 + i * 2};
		recs[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = partw + 1 + i * 2, .height = 1};
	}
	val.foreground = decor[COLOR_DARK].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);

	free(recs);
}

/* draw and fill rectangle */
void
drawbackground(Pixmap pix, int x, int y, int w, int h, int style)
{
	drawrectangle(pix, x, y, w, h, theme.colors[style][COLOR_MID].pixel);
}

/* draw rectangle shadows */
void
drawshadow(Pixmap pix, int x, int y, int w, int h, int style, int pressed)
{
	XGCValues val;
	XRectangle *recs;
	unsigned long top, bot;
	int i;

	if (w <= 0 || h <= 0)
		return;

	top = theme.colors[style][pressed ? COLOR_DARK : COLOR_LIGHT].pixel;
	bot = theme.colors[style][pressed ? COLOR_LIGHT : COLOR_DARK].pixel;
	recs = ecalloc(config.shadowthickness * 2, sizeof(*recs));

	/* draw light shadow */
	for(i = 0; i < config.shadowthickness; i++) {
		recs[i * 2]     = (XRectangle){.x = x + i, .y = y + i, .width = 1, .height = h - (i * 2 + 1)};
		recs[i * 2 + 1] = (XRectangle){.x = x + i, .y = y + i, .width = w - (i * 2 + 1), .height = 1};
	}
	val.foreground = top;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 2);

	/* draw dark shadow */
	for(i = 0; i < config.shadowthickness; i++) {
		recs[i * 2]     = (XRectangle){.x = x + w - 1 - i, .y = y + i,         .width = 1,     .height = h - i * 2};
		recs[i * 2 + 1] = (XRectangle){.x = x + i,         .y = y + h - 1 - i, .width = w - i * 2, .height = 1};
	}
	val.foreground = bot;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 2);

	free(recs);
}

void
drawframe(Pixmap pix, int isshaded, int w, int h, enum Octant o, int style)
{
	XRectangle *recs;
	XGCValues val;
	XftColor *decor;
	int x, y, i;

	decor = theme.colors[style];
	recs = ecalloc(config.shadowthickness * 5, sizeof(*recs));

	/* top edge */
	drawshadow(pix, config.corner, 0, w - config.corner * 2, config.borderwidth, style, o == N);

	/* bottom edge */
	drawshadow(pix, config.corner, h - config.borderwidth, w - config.corner * 2, config.borderwidth, style, o == S);

	/* left edge */
	drawshadow(pix, 0, config.corner, config.borderwidth, h - config.corner * 2, style, o == W);

	/* left edge */
	drawshadow(pix, w - config.borderwidth, config.corner, config.borderwidth, h - config.corner * 2, style, o == E);

	if (isshaded) {
		/* left corner */
		x = 0;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + i, .y = 0, .width = 1,                     .height = h - 1 - i};
			recs[i * 3 + 1] = (XRectangle){.x = x + 0, .y = i, .width = config.corner - 1 - i, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = config.titlewidth, .height = 1};
		}
		val.foreground = (o & W) ? decor[COLOR_DARK].pixel : decor[COLOR_LIGHT].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 5 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i,    .width = 1,                         .height = h - config.borderwidth * 2 + 1 + i * 2};
			recs[i * 5 + 1] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i,    .width = config.titlewidth + 1 + i, .height = 1};
			recs[i * 5 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = i,                             .width = 1,                         .height = config.borderwidth - i};
			recs[i * 5 + 3] = (XRectangle){.x = x + config.corner - 1 - i,      .y = h - config.borderwidth + i, .width = 1,                         .height = config.borderwidth - i};
			recs[i * 5 + 4] = (XRectangle){.x = x + i,                          .y = h - 1 - i,                  .width = config.corner - i,         .height = 1};
		}
		val.foreground = (o & W) ? decor[COLOR_LIGHT].pixel : decor[COLOR_DARK].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 5);

		/* right corner */
		x = w - config.corner;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 5 + 0] = (XRectangle){.x = x + i,                     .y = 0,                             .width = 1,                     .height = config.borderwidth - 1 - i};
			recs[i * 5 + 1] = (XRectangle){.x = x + 0,                     .y = i,                             .width = config.corner - 1 - i, .height = 1};
			recs[i * 5 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = config.borderwidth - 1 - i,    .width = 1,                     .height = h - config.borderwidth * 2 + 1 + i * 2};
			recs[i * 5 + 3] = (XRectangle){.x = x + i,                     .y = h - config.borderwidth + i, .width = config.titlewidth + 1, .height = 1};
			recs[i * 5 + 4] = (XRectangle){.x = x + i,                     .y = h - config.borderwidth + i, .width = 1,                     .height = config.borderwidth - 1 - i * 2};
		}
		val.foreground = (o == E) ? decor[COLOR_DARK].pixel : decor[COLOR_LIGHT].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 5);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = i,                          .width = 1,                 .height = h - i};
			recs[i * 3 + 1] = (XRectangle){.x = x + i,                     .y = config.borderwidth - 1 - i, .width = config.titlewidth, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + i,                     .y = h - 1 - i,               .width = config.corner - i, .height = 1};
		}
		val.foreground = (o == E) ? decor[COLOR_LIGHT].pixel : decor[COLOR_DARK].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
	} else {
		/* top left corner */
		x = y = 0;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 2 + 0] = (XRectangle){.x = x + i, .y = y + 0, .width = 1,                     .height = config.corner - 1 - i};
			recs[i * 2 + 1] = (XRectangle){.x = x + 0, .y = y + i, .width = config.corner - 1 - i, .height = 1};
		}
		val.foreground = (o == NW) ? decor[COLOR_DARK].pixel : decor[COLOR_LIGHT].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 2);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 4 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = 1,                         .height = config.titlewidth + 1 + i};
			recs[i * 4 + 1] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = config.titlewidth + 1 + i, .height = 1};
			recs[i * 4 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + i,                          .width = 1,                         .height = config.borderwidth - i};
			recs[i * 4 + 3] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i,      .width = config.borderwidth - i,    .height = 1};
		}
		val.foreground = (o == NW) ? decor[COLOR_LIGHT].pixel : decor[COLOR_DARK].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);

		/* bottom left corner */
		x = 0;
		y = h - config.corner;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + i,                          .y = y + 0,                     .width = 1,                          .height = config.corner - 1 - i};
			recs[i * 3 + 1] = (XRectangle){.x = x + 0,                          .y = y + i,                     .width = config.borderwidth - 1 - i, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.titlewidth + i, .width = config.titlewidth,          .height = 1};
		}
		val.foreground = (o == SW) ? decor[COLOR_DARK].pixel : decor[COLOR_LIGHT].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + i,                     .width = 1,                 .height = config.titlewidth};
			recs[i * 3 + 1] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i, .width = config.corner - i, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + config.titlewidth + i, .width = 1,                 .height = config.borderwidth - i};
		}
		val.foreground = (o == SW) ? decor[COLOR_LIGHT].pixel : decor[COLOR_DARK].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);

		/* top right corner */
		x = w - config.corner;
		y = 0;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + i,                     .y = y + 0,                          .width = 1,                     .height = config.borderwidth - 1 - i};
			recs[i * 3 + 1] = (XRectangle){.x = x + 0,                     .y = y + i,                          .width = config.corner - 1 - i, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.borderwidth - 1 - i, .width = 1,                     .height = config.titlewidth};
		}
		val.foreground = (o == NE) ? decor[COLOR_DARK].pixel : decor[COLOR_LIGHT].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                          .width = 1,                      .height = config.corner};
			recs[i * 3 + 1] = (XRectangle){.x = x + i,                     .y = y + config.borderwidth - 1 - i, .width = config.titlewidth,      .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.corner - 1 - i,      .width = config.borderwidth - i, .height = 1};
		}
		val.foreground = (o == NE) ? decor[COLOR_LIGHT].pixel : decor[COLOR_DARK].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);

		/* bottom right corner */
		x = w - config.corner;
		y = h - config.corner;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 4 + 0] = (XRectangle){.x = x + i,                     .y = y + config.titlewidth + i,  .width = 1,                              .height = config.borderwidth - 1 - i * 2};
			recs[i * 4 + 1] = (XRectangle){.x = x + config.titlewidth + i, .y = y + i,                      .width = config.borderwidth - 1 - i * 2, .height = 1};
			recs[i * 4 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + i,                      .width = 1,                              .height = config.titlewidth + 1};
			recs[i * 4 + 3] = (XRectangle){.x = x + i,                     .y = y + config.titlewidth + i,  .width = config.titlewidth + 1,          .height = 1};
		}
		val.foreground = (o == SE) ? decor[COLOR_DARK].pixel : decor[COLOR_LIGHT].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 2 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                     .width = 1,                      .height = config.corner - i};
			recs[i * 2 + 1] = (XRectangle){.x = x + i,                     .y = y + config.corner - 1 - i, .width = config.corner - i,      .height = 1};
		}
		val.foreground = (o == SE) ? decor[COLOR_LIGHT].pixel : decor[COLOR_DARK].pixel;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 2);
	}
	free(recs);
}

void
drawprompt(Pixmap pix, int w, int h)
{
	XGCValues val;
	XRectangle *recs;
	int i, partw, parth;

	recs = ecalloc(config.shadowthickness * 3, sizeof(*recs));
	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 3 + 0] = (XRectangle){.x = i,                          .y = i,                          .width = 1,                 .height = h - 1 - i};
		recs[i * 3 + 1] = (XRectangle){.x = w - config.borderwidth + i, .y = 0,                          .width = 1,                 .height = parth + config.borderwidth + i};
		recs[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 + i * 2, .height = 1};
	}
	val.foreground = theme.colors[FOCUSED][COLOR_LIGHT].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 3 + 0] = (XRectangle){.x = w - 1 - i,                  .y = i,         .width = 1,         .height = h - i * 2};
		recs[i * 3 + 1] = (XRectangle){.x = i,                          .y = h - 1 - i, .width = w - i * 2, .height = 1};
		recs[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = i,         .width = 1,         .height = parth + config.borderwidth};
	}
	val.foreground = theme.colors[FOCUSED][COLOR_DARK].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);

	free(recs);
}

void
drawdock(Pixmap pix, int w, int h)
{
	XGCValues val;
	XRectangle *recs;
	int i;

	if (pix == None || w <= 0 || h <= 0)
		return;
	recs = ecalloc(DOCKBORDER * 3, sizeof(*recs));

	val.foreground = theme.colors[STYLE_OTHER][COLOR_BG].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, w, h);

	val.foreground = theme.colors[STYLE_OTHER][COLOR_BORD].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);

	if (config.dockgravity[0] != '\0' && (config.dockgravity[1] == 'F' || config.dockgravity[1] == 'f')) {
		switch (config.dockgravity[0]) {
		case 'N':
			XFillRectangle(dpy, pix, gc, 0, h - DOCKBORDER, w, DOCKBORDER);
			break;
		case 'S':
			XFillRectangle(dpy, pix, gc, 0, 0, w, 1);
			break;
		case 'W':
			XFillRectangle(dpy, pix, gc, w - DOCKBORDER, 0, DOCKBORDER, h);
			break;
		default:
		case 'E':
			XFillRectangle(dpy, pix, gc, 0, 0, DOCKBORDER, h);
			break;
		}
		return;
	}

	switch (config.dockgravity[0]) {
	case 'N':
		for(i = 0; i < DOCKBORDER; i++) {
			recs[i * 3 + 0] = (XRectangle){
				.x = i,
				.y = 0,
				.width = 1,
				.height = h
			};
			recs[i * 3 + 1] = (XRectangle){
				.x = 0,
				.y = h - 1 - i,
				.width = w,
				.height = 1
			};
			recs[i * 3 + 2] = (XRectangle){
				.x = w - 1 - i,
				.y = 0,
				.width = 1,
				.height = h
			};
		}
		break;
	case 'W':
		for(i = 0; i < DOCKBORDER; i++) {
			recs[i * 3 + 0] = (XRectangle){
				.x = 0,
				.y = i,
				.width = w,
				.height = 1
			};
			recs[i * 3 + 1] = (XRectangle){
				.x = w - 1 - i,
				.y = 0,
				.width = 1,
				.height = h
			};
			recs[i * 3 + 2] = (XRectangle){
				.x = 0,
				.y = h - 1 - i,
				.width = w,
				.height = 1
			};
		}
		break;
	case 'S':
		for(i = 0; i < DOCKBORDER; i++) {
			recs[i * 3 + 0] = (XRectangle){
				.x = i,
				.y = 0,
				.width = 1,
				.height = h
			};
			recs[i * 3 + 1] = (XRectangle){
				.x = 0,
				.y = i,
				.width = w,
				.height = 1
			};
			recs[i * 3 + 2] = (XRectangle){
				.x = w - 1 - i,
				.y = 0,
				.width = 1,
				.height = h
			};
		}
		break;
	default:
	case 'E':
		for(i = 0; i < DOCKBORDER; i++) {
			recs[i * 3 + 0] = (XRectangle){
				.x = 0,
				.y = i,
				.width = w,
				.height = 1
			};
			recs[i * 3 + 1] = (XRectangle){
				.x = i,
				.y = 0,
				.width = 1,
				.height = h
			};
			recs[i * 3 + 2] = (XRectangle){
				.x = 0,
				.y = h - 1 - i,
				.width = w,
				.height = 1
			};
		}
		break;
	}
	XFillRectangles(dpy, pix, gc, recs, DOCKBORDER * 3);
	free(recs);
}

/* draw title bar buttons */
void
buttonleftdecorate(Window button, Pixmap pix, int style, int pressed)
{
	XGCValues val;
	XRectangle recs[2];
	unsigned long top, bot;
	int x, y, w;

	w = config.titlewidth - 9;
	if (pressed) {
		top = theme.colors[style][COLOR_DARK].pixel;
		bot = theme.colors[style][COLOR_LIGHT].pixel;
	} else {
		top = theme.colors[style][COLOR_LIGHT].pixel;
		bot = theme.colors[style][COLOR_DARK].pixel;
	}

	/* draw background */
	drawbackground(pix, 0, 0, config.titlewidth, config.titlewidth, style);
	drawshadow(pix, 0, 0, config.titlewidth, config.titlewidth, style, pressed);

	if (w > 0) {
		x = 4;
		y = config.titlewidth / 2 - 1;
		recs[0] = (XRectangle){.x = x, .y = y, .width = w, .height = 1};
		recs[1] = (XRectangle){.x = x, .y = y, .width = 1, .height = 3};
		val.foreground = (pressed) ? bot : top;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, 2);
		recs[0] = (XRectangle){.x = x + 1, .y = y + 2, .width = w, .height = 1};
		recs[1] = (XRectangle){.x = x + w, .y = y, .width = 1, .height = 3};
		val.foreground = (pressed) ? top : bot;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, 2);
	}

	drawcommit(pix, button);
}

/* draw title bar buttons */
void
buttonrightdecorate(Window button, Pixmap pix, int style, int pressed)
{
	XGCValues val;
	XPoint pts[9];
	unsigned long mid, top, bot;
	int w;

	w = (config.titlewidth - 11) / 2;
	mid = theme.colors[style][COLOR_MID].pixel;
	if (pressed) {
		top = theme.colors[style][COLOR_DARK].pixel;
		bot = theme.colors[style][COLOR_LIGHT].pixel;
	} else {
		top = theme.colors[style][COLOR_LIGHT].pixel;
		bot = theme.colors[style][COLOR_DARK].pixel;
	}

	/* draw background */
	val.foreground = mid;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, config.titlewidth, config.titlewidth);

	drawshadow(pix, 0, 0, config.titlewidth, config.titlewidth, style, pressed);

	if (w > 0) {
		pts[0] = (XPoint){.x = 3, .y = config.titlewidth - 5};
		pts[1] = (XPoint){.x = 0, .y = - 1};
		pts[2] = (XPoint){.x = w, .y = -w};
		pts[3] = (XPoint){.x = -w, .y = -w};
		pts[4] = (XPoint){.x = 0, .y = -2};
		pts[5] = (XPoint){.x = 2, .y = 0};
		pts[6] = (XPoint){.x = w, .y = w};
		pts[7] = (XPoint){.x = w, .y = -w};
		pts[8] = (XPoint){.x = 1, .y = 0};
		val.foreground = (pressed) ? bot : top;
		XChangeGC(dpy, gc, GCForeground, &val);
		XDrawLines(dpy, pix, gc, pts, 9, CoordModePrevious);

		pts[0] = (XPoint){.x = 3, .y = config.titlewidth - 4};
		pts[1] = (XPoint){.x = 2, .y = 0};
		pts[2] = (XPoint){.x = w, .y = -w};
		pts[3] = (XPoint){.x = w, .y = w};
		pts[4] = (XPoint){.x = 2, .y = 0};
		pts[5] = (XPoint){.x = 0, .y = -2};
		pts[6] = (XPoint){.x = -w, .y = -w};
		pts[7] = (XPoint){.x = w, .y = -w};
		pts[8] = (XPoint){.x = 0, .y = -2};
		val.foreground = (pressed) ? top : bot;
		XChangeGC(dpy, gc, GCForeground, &val);
		XDrawLines(dpy, pix, gc, pts, 9, CoordModePrevious);
	}

	drawcommit(pix, button);
}

static void
reloadtheme(void)
{
	Pixmap pix;

	pix = XCreatePixmap(
		dpy,
		wm.dragwin,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		depth
	);
	config.corner = config.borderwidth + config.titlewidth;
	config.divwidth = config.borderwidth;
	wm.minsize = config.corner * 2 + 10;
	drawbackground(
		pix,
		0, 0,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		FOCUSED
	);
	drawborders(
		pix,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		FOCUSED
	);
	drawshadow(
		pix,
		config.borderwidth,
		config.borderwidth,
		config.titlewidth,
		config.titlewidth,
		FOCUSED,
		0
	);
	XMoveResizeWindow(
		dpy,
		wm.dragwin,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth),
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth
	);
	XSetWindowBackgroundPixmap(dpy, wm.dragwin, pix);
	XClearWindow(dpy, wm.dragwin);
	XFreePixmap(dpy, pix);
}

/* initialize decoration pixmap */
int
settheme(void)
{
	int i, j, error;

	error = 0;
	gc = XCreateGC(dpy, wm.dragwin, GCFillStyle, &(XGCValues){.fill_style = FillSolid});
	for (i = 0; i < STYLE_LAST; i++)
		for (j = 0; j < COLOR_LAST; j++)
			if (!alloccolor(config.colors[i][j], &theme.colors[i][j]))
				error = 1;
	if ((theme.font = openfont(config.font)) == NULL)
		error = 1;
	if (error)
		return 0;
	reloadtheme();
	return 1;
}

/* free font */
void
cleantheme(void)
{
	int i, j;

	XftFontClose(dpy, theme.font);
	for (i = 0; i < STYLE_LAST; i++)
		for (j = 0; j < COLOR_LAST; j++)
			XftColorFree(dpy, visual, colormap, &theme.colors[i][j]);
	XFreeGC(dpy, gc);
}

static char *
queryrdb(int res)
{
	XrmClass class[] = { wm.application.class, wm.resources[res].class, NULLQUARK };
	XrmName name[] = { wm.application.name, wm.resources[res].name, NULLQUARK };

	return getresource(xdb, class, name);
}

static void
setcolor(char *value, int style, int ncolor)
{
	XftColor color;

	if (!alloccolor(value, &color))
		return;
	XftColorFree(dpy, visual, colormap, &theme.colors[style][ncolor]);
	theme.colors[style][ncolor] = color;
}

void
setresources(char *xrm)
{
	XftFont *font;
	long n;
	char *value;
	enum Resource resource;

	xdb = NULL;
	if (xrm == NULL || (xdb = XrmGetStringDatabase(xrm)) == NULL)
		return;
	for (resource = 0; resource < NRESOURCES; resource++) {
		value = queryrdb(resource);
		if (value == NULL)
			continue;
		switch (resource) {
		case RES_FACE_NAME:
			if ((font = openfont(value)) != NULL) {
				XftFontClose(dpy, theme.font);
				theme.font = font;
			}
			break;
		case RES_FOREGROUND:
			setcolor(value, STYLE_OTHER, COLOR_FG);
			break;
		case RES_DOCK_BACKGROUND:
			setcolor(value, STYLE_OTHER, COLOR_BG);
			break;
		case RES_DOCK_BORDER:
			setcolor(value, STYLE_OTHER, COLOR_BORD);
			break;
		case RES_ACTIVE_BG:
			setcolor(value, FOCUSED, COLOR_MID);
			break;
		case RES_ACTIVE_TOP:
			setcolor(value, FOCUSED, COLOR_LIGHT);
			break;
		case RES_ACTIVE_BOT:
			setcolor(value, FOCUSED, COLOR_DARK);
			break;
		case RES_INACTIVE_BG:
			setcolor(value, UNFOCUSED, COLOR_MID);
			break;
		case RES_INACTIVE_TOP:
			setcolor(value, UNFOCUSED, COLOR_LIGHT);
			break;
		case RES_INACTIVE_BOT:
			setcolor(value, UNFOCUSED, COLOR_DARK);
			break;
		case RES_URGENT_BG:
			setcolor(value, URGENT, COLOR_MID);
			break;
		case RES_URGENT_TOP:
			setcolor(value, URGENT, COLOR_LIGHT);
			break;
		case RES_URGENT_BOT:
			setcolor(value, URGENT, COLOR_DARK);
			break;
		case RES_BORDER_WIDTH:
			if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
				config.borderwidth = n;
			break;
		case RES_SHADOW_WIDTH:
			if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
				config.shadowthickness = n;
			break;
		case RES_TITLE_WIDTH:
			if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
				config.titlewidth = n;
			break;
		case RES_DOCK_WIDTH:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.dockwidth = n;
			break;
		case RES_DOCK_SPACE:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.dockspace = n;
			break;
		case RES_DOCK_GRAVITY:
			config.dockgravity = value;
			break;
		case RES_NOTIFY_GAP:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.notifgap = n;
			break;
		case RES_NOTIFY_GRAVITY:
			config.notifgravity = value;
			break;
		case RES_SNAP_PROXIMITY:
			if ((n = strtol(value, NULL, 10)) >= 0 && n < 100)
				config.snap = n;
			break;
		case RES_MOVE_TIME:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.movetime = n;
			break;
		case RES_RESIZE_TIME:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.resizetime = n;
			break;
		default:
			break;
		}
	}
	reloadtheme();
}
