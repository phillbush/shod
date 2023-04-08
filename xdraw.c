#include <err.h>

#include "shod.h"

static GC gc;
static struct Theme {
	XftFont *font;
	XftColor fg[STYLE_LAST][2];
	unsigned long border[STYLE_LAST][COLOR_LAST];
	unsigned long dock[2];
} theme;

/* get color from color string */
static unsigned long
ealloccolor(const char *s)
{
	XColor color;

	if(!XAllocNamedColor(dpy, colormap, s, &color, &color)) {
		warnx("could not allocate color: %s", s);
		return BlackPixel(dpy, screen);
	}
	return color.pixel;
}

/* get XftColor from color string */
static void
eallocxftcolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "could not allocate color: %s", s);
}

/* win was exposed, return the pixmap of its contents and the pixmap's size */
static int
getexposed(Window win, Pixmap *pix, int *pw, int *ph)
{
	struct Object *n, *t, *d, *m;
	struct Container *c;
	struct Column *col;
	struct Row *row;
	struct Tab *tab;
	struct Dialog *dial;
	struct Menu *menu;
	struct Notification *notif;

	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (c->frame == win) {
			*pix = c->pix;
			*pw = c->pw;
			*ph = c->ph;
			return 1;
		}
		TAILQ_FOREACH(col, &(c)->colq, entry) {
			TAILQ_FOREACH(row, &col->rowq, entry) {
				if (row->bar == win) {
					*pix = row->pixbar;
					*pw = row->pw;
					*ph = config.titlewidth;
					return 1;
				}
				if (row->bl == win) {
					*pix = row->pixbl;
					*pw = config.titlewidth;
					*ph = config.titlewidth;
					return 1;
				}
				if (row->br == win) {
					*pix = row->pixbr;
					*pw = config.titlewidth;
					*ph = config.titlewidth;
					return 1;
				}
				TAILQ_FOREACH(t, &row->tabq, entry) {
					tab = (struct Tab *)t;
					if (tab->frame == win) {
						*pix = tab->pix;
						*pw = tab->pw;
						*ph = tab->ph;
						return 1;
					}
					if (tab->title == win) {
						*pix = tab->pixtitle;
						*pw = tab->ptw;
						*ph = config.titlewidth;
						return 1;
					}
					TAILQ_FOREACH(d, &tab->dialq, entry) {
						dial = (struct Dialog *)d;
						if (dial->frame == win) {
							*pix = dial->pix;
							*pw = dial->pw;
							*ph = dial->ph;
							return 1;
						}
					}
				}
			}
		}
	}
	if (dock.win == win) {
		*pix = dock.pix;
		*pw = dock.w;
		*ph = dock.h;
		return 1;
	}
	TAILQ_FOREACH(m, &wm.menuq, entry) {
		menu = (struct Menu *)m;
		if (menu->frame == win) {
			*pix = menu->pix;
			*pw = menu->pw;
			*ph = menu->ph;
			return 1;
		}
		if (menu->titlebar == win) {
			*pix = menu->pixtitlebar;
			*pw = menu->tw;
			*ph = menu->th;
			return 1;
		}
		if (menu->button == win) {
			*pix = menu->pixbutton;
			*pw = config.titlewidth;
			*ph = config.titlewidth;
			return 1;
		}
	}
	TAILQ_FOREACH(n, &wm.notifq, entry) {
		notif = (struct Notification *)n;
		if (notif->frame == win) {
			*pix = notif->frame;
			*pw = notif->pw;
			*ph = notif->ph;
			return 1;
		}
	}
	return 0;
}

void
pixmapnew(Pixmap *pix, Window win, int w, int h)
{
	if (*pix != None)
		XFreePixmap(dpy, *pix);
	*pix = XCreatePixmap(dpy, win, w, h, depth);
}

void
drawcommit(Pixmap pix, Window win, int w, int h)
{
	XCopyArea(dpy, pix, win, gc, 0, 0, w, h, 0, 0);
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

	top = theme.border[style][pressed ? COLOR_DARK : COLOR_LIGHT];
	bot = theme.border[style][pressed ? COLOR_LIGHT : COLOR_DARK];
	color = &theme.fg[style][ismenu ? 1 : drawlines];
	draw = XftDrawCreate(dpy, pix, visual, colormap);
	len = strlen(text);
	XftTextExtentsUtf8(dpy, theme.font, text, len, &box);
	x = max(0, (w - box.width) / 2 + box.x);
	y = (config.titlewidth - box.height) / 2 + box.y;
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
	XftDrawStringUtf8(draw, color, theme.font, x, y, text, len);
	XftDrawDestroy(draw);
}

/* draw borders with shadows */
void
drawborders(Pixmap pix, int w, int h, int style)
{
	XGCValues val;
	XRectangle *recs;
	unsigned long *decor;
	int partw, parth;
	int i;

	if (w <= 0 || h <= 0)
		return;

	decor = theme.border[style];
	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	recs = ecalloc(config.shadowthickness * 4, sizeof(*recs));

	/* draw background */
	val.foreground = decor[COLOR_MID];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, w, h);

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 4 + 0] = (XRectangle){.x = i, .y = i, .width = 1, .height = h - 1 - i};
		recs[i * 4 + 1] = (XRectangle){.x = i, .y = i, .width = w - 1 - i, .height = 1};
		recs[i * 4 + 2] = (XRectangle){.x = w - config.borderwidth + i, .y = config.borderwidth - 1 - i, .width = 1, .height = parth + 2 * (i + 1)};
		recs[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 * (i + 1), .height = 1};
	}
	val.foreground = decor[COLOR_LIGHT];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 4 + 0] = (XRectangle){.x = w - 1 - i, .y = i,         .width = 1,         .height = h - i * 2};
		recs[i * 4 + 1] = (XRectangle){.x = i,         .y = h - 1 - i, .width = w - i * 2, .height = 1};
		recs[i * 4 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = 1,                 .height = parth + 1 + i * 2};
		recs[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = partw + 1 + i * 2, .height = 1};
	}
	val.foreground = decor[COLOR_DARK];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);

	free(recs);
}

/* draw and fill rectangle */
void
drawbackground(Pixmap pix, int x, int y, int w, int h, int style)
{
	XGCValues val;

	val.foreground = theme.border[style][COLOR_MID];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, x, y, w, h);
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

	top = theme.border[style][pressed ? COLOR_DARK : COLOR_LIGHT];
	bot = theme.border[style][pressed ? COLOR_LIGHT : COLOR_DARK];
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
	unsigned long *decor;
	int x, y, i;

	decor = theme.border[style];
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
		val.foreground = (o & W) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 5 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i,    .width = 1,                         .height = h - config.borderwidth * 2 + 1 + i * 2};
			recs[i * 5 + 1] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i,    .width = config.titlewidth + 1 + i, .height = 1};
			recs[i * 5 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = i,                             .width = 1,                         .height = config.borderwidth - i};
			recs[i * 5 + 3] = (XRectangle){.x = x + config.corner - 1 - i,      .y = h - config.borderwidth + i, .width = 1,                         .height = config.borderwidth - i};
			recs[i * 5 + 4] = (XRectangle){.x = x + i,                          .y = h - 1 - i,                  .width = config.corner - i,         .height = 1};
		}
		val.foreground = (o & W) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
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
		val.foreground = (o == E) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 5);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = i,                          .width = 1,                 .height = h - i};
			recs[i * 3 + 1] = (XRectangle){.x = x + i,                     .y = config.borderwidth - 1 - i, .width = config.titlewidth, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + i,                     .y = h - 1 - i,               .width = config.corner - i, .height = 1};
		}
		val.foreground = (o == E) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
	} else {
		/* top left corner */
		x = y = 0;
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 2 + 0] = (XRectangle){.x = x + i, .y = y + 0, .width = 1,                     .height = config.corner - 1 - i};
			recs[i * 2 + 1] = (XRectangle){.x = x + 0, .y = y + i, .width = config.corner - 1 - i, .height = 1};
		}
		val.foreground = (o == NW) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 2);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 4 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = 1,                         .height = config.titlewidth + 1 + i};
			recs[i * 4 + 1] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = config.titlewidth + 1 + i, .height = 1};
			recs[i * 4 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + i,                          .width = 1,                         .height = config.borderwidth - i};
			recs[i * 4 + 3] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i,      .width = config.borderwidth - i,    .height = 1};
		}
		val.foreground = (o == NW) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
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
		val.foreground = (o == SW) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + i,                     .width = 1,                 .height = config.titlewidth};
			recs[i * 3 + 1] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i, .width = config.corner - i, .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + config.titlewidth + i, .width = 1,                 .height = config.borderwidth - i};
		}
		val.foreground = (o == SW) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
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
		val.foreground = (o == NE) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 3 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                          .width = 1,                      .height = config.corner};
			recs[i * 3 + 1] = (XRectangle){.x = x + i,                     .y = y + config.borderwidth - 1 - i, .width = config.titlewidth,      .height = 1};
			recs[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.corner - 1 - i,      .width = config.borderwidth - i, .height = 1};
		}
		val.foreground = (o == NE) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
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
		val.foreground = (o == SE) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 4);
		for (i = 0; i < config.shadowthickness; i++) {
			recs[i * 2 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                     .width = 1,                      .height = config.corner - i};
			recs[i * 2 + 1] = (XRectangle){.x = x + i,                     .y = y + config.corner - 1 - i, .width = config.corner - i,      .height = 1};
		}
		val.foreground = (o == SE) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
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
	val.foreground = theme.border[FOCUSED][COLOR_LIGHT];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, config.shadowthickness * 3);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		recs[i * 3 + 0] = (XRectangle){.x = w - 1 - i,                  .y = i,         .width = 1,         .height = h - i * 2};
		recs[i * 3 + 1] = (XRectangle){.x = i,                          .y = h - 1 - i, .width = w - i * 2, .height = 1};
		recs[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = i,         .width = 1,         .height = parth + config.borderwidth};
	}
	val.foreground = theme.border[FOCUSED][COLOR_DARK];
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

	val.foreground = theme.dock[COLOR_DEF];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, w, h);

	val.foreground = theme.dock[COLOR_ALT];
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
		top = theme.border[style][COLOR_DARK];
		bot = theme.border[style][COLOR_LIGHT];
	} else {
		top = theme.border[style][COLOR_LIGHT];
		bot = theme.border[style][COLOR_DARK];
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

	drawcommit(pix, button, config.titlewidth, config.titlewidth);
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
	mid = theme.border[style][COLOR_MID];
	if (pressed) {
		top = theme.border[style][COLOR_DARK];
		bot = theme.border[style][COLOR_LIGHT];
	} else {
		top = theme.border[style][COLOR_LIGHT];
		bot = theme.border[style][COLOR_DARK];
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

	drawcommit(pix, button, config.titlewidth, config.titlewidth);
}

/* copy pixmap into exposed window */
void
copypixmap(Window win)
{
	Pixmap pix;
	int pw, ph;

	if (getexposed(win, &pix, &pw, &ph)) {
		drawcommit(pix, win, pw, ph);
	}
}

/* initialize decoration pixmap */
void
inittheme(void)
{
	Pixmap pix;
	int i, j;

	pix = XCreatePixmap(
		dpy,
		wm.dragwin,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		depth
	);
	gc = XCreateGC(dpy, wm.dragwin, GCFillStyle, &(XGCValues){.fill_style = FillSolid});
	config.corner = config.borderwidth + config.titlewidth;
	config.divwidth = config.borderwidth;
	wm.minsize = config.corner * 2 + 10;
	for (i = 0; i < STYLE_LAST; i++) {
		for (j = 0; j < COLOR_LAST; j++) {
			theme.border[i][j] = ealloccolor(config.bordercolors[i][j]);
		}
		eallocxftcolor(config.bordercolors[i][COLOR_LIGHT], &theme.fg[i][0]);
		eallocxftcolor(config.foreground, &theme.fg[i][1]);
	}
	for (j = 0; j < 2; j++)
		theme.dock[j]  = ealloccolor(config.dockcolors[j]);
	theme.font = XftFontOpenXlfd(dpy, screen, config.font);
	if (theme.font == NULL) {
		theme.font = XftFontOpenName(dpy, screen, config.font);
		if (theme.font == NULL) {
			errx(1, "could not open font: %s", config.font);
		}
	}
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
	XSetWindowBackgroundPixmap(dpy, wm.dragwin, pix);
	XClearWindow(dpy, wm.dragwin);
	XFreePixmap(dpy, pix);
}

/* free font */
void
cleantheme(void)
{
	int i;

	XftFontClose(dpy, theme.font);
	for (i = 0; i < STYLE_LAST; i++) {
		XFreeColors(dpy, colormap, theme.border[i], COLOR_LAST, 0);
		XftColorFree(dpy, visual, colormap, &theme.fg[i][0]);
		XftColorFree(dpy, visual, colormap, &theme.fg[i][1]);
	}
	XFreeColors(dpy, colormap, theme.dock, 2, 0);
	XFreeGC(dpy, gc);
}

static char *
queryrdb(int res)
{
	XrmClass class[] = { wm.application.class, wm.resources[res].class, NULLQUARK };
	XrmName name[] = { wm.application.name, wm.resources[res].name, NULLQUARK };

	return getresource(xdb, class, name);
}

void
setresources(char *xrm)
{
	long n;
	char *value;

	if (xrm == NULL || (xdb = XrmGetStringDatabase(xrm)) == NULL)
		return;

	if ((value = queryrdb(RES_FACE_NAME)) != NULL)
		config.font = value;
	if ((value = queryrdb(RES_FOREGROUND)) != NULL)
		config.foreground = value;

	if ((value = queryrdb(RES_DOCK_BACKGROUND)) != NULL)
		config.dockcolors[COLOR_DEF] = value;
	if ((value = queryrdb(RES_DOCK_BORDER)) != NULL)
		config.dockcolors[COLOR_ALT] = value;

	if ((value = queryrdb(RES_ACTIVE_BG)) != NULL)
		config.bordercolors[FOCUSED][COLOR_MID] = value;
	if ((value = queryrdb(RES_ACTIVE_TOP)) != NULL)
		config.bordercolors[FOCUSED][COLOR_LIGHT] = value;
	if ((value = queryrdb(RES_ACTIVE_BOT)) != NULL)
		config.bordercolors[FOCUSED][COLOR_DARK] = value;

	if ((value = queryrdb(RES_INACTIVE_BG)) != NULL)
		config.bordercolors[UNFOCUSED][COLOR_MID] = value;
	if ((value = queryrdb(RES_INACTIVE_TOP)) != NULL)
		config.bordercolors[UNFOCUSED][COLOR_LIGHT] = value;
	if ((value = queryrdb(RES_INACTIVE_BOT)) != NULL)
		config.bordercolors[UNFOCUSED][COLOR_DARK] = value;

	if ((value = queryrdb(RES_URGENT_BG)) != NULL)
		config.bordercolors[URGENT][COLOR_MID] = value;
	if ((value = queryrdb(RES_URGENT_TOP)) != NULL)
		config.bordercolors[URGENT][COLOR_LIGHT] = value;
	if ((value = queryrdb(RES_URGENT_BOT)) != NULL)
		config.bordercolors[URGENT][COLOR_DARK] = value;

	if ((value = queryrdb(RES_BORDER_WIDTH)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
			config.borderwidth = n;
	if ((value = queryrdb(RES_SHADOW_WIDTH)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
			config.shadowthickness = n;
	if ((value = queryrdb(RES_TITLE_WIDTH)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
			config.titlewidth = n;
	if ((value = queryrdb(RES_DOCK_WIDTH)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0)
			config.dockwidth = n;
	if ((value = queryrdb(RES_DOCK_SPACE)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0)
			config.dockspace = n;
	if ((value = queryrdb(RES_DOCK_GRAVITY)) != NULL)
		config.dockgravity = value;
	if ((value = queryrdb(RES_NOTIFY_GAP)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0)
			config.notifgap = n;
	if ((value = queryrdb(RES_NOTIFY_GRAVITY)) != NULL)
		config.notifgravity = value;
	if ((value = queryrdb(RES_NDESKTOPS)) != NULL)
		if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
			config.ndesktops = n;
	if ((value = queryrdb(RES_SNAP_PROXIMITY)) != NULL)
		if ((n = strtol(value, NULL, 10)) >= 0 && n < 100)
			config.snap = n;
}
