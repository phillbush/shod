#include <err.h>
#include <stdlib.h>

#include "shod.h"

Visual *visual;
Colormap colormap;
unsigned int depth;
XrmDatabase xdb = NULL;

#define MOUSE_EVENTS (ButtonReleaseMask|ButtonPressMask|Button1MotionMask)
#define MIN_SHADOW_THICKNESS 1
#define MAX_SHADOW_THICKNESS 2

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

static Window
createwindow(Window parent, XRectangle geom, long mask, XSetWindowAttributes *attrs)
{
	mask |= CWColormap | CWBackPixel | CWBorderPixel;
	attrs->colormap = colormap;
	attrs->border_pixel = BlackPixel(dpy, screen);
	attrs->background_pixel = BlackPixel(dpy, screen);
	return XCreateWindow(
		dpy, parent,
		geom.x, geom.y, geom.width, geom.height, 0,
		depth, InputOutput, visual, mask, attrs
	);
}

static void
drawrectangle(Pixmap pix, int x, int y, int w, int h, unsigned long color)
{
	XChangeGC(
		dpy, gc,
		GCForeground,
		&(XGCValues){ .foreground = color }
	);
	XFillRectangle(dpy, pix, gc, x, y, w, h);
}

static void
draw_button_left(Pixmap pix, int style)
{
	XGCValues val;
	XRectangle rects[2];
	unsigned long top, bot;
	int x, y, w;
	Bool pressed;

	w = config.titlewidth - 9;
	if (style == PRESSED) {
		pressed = True;
		style = FOCUSED;
		top = theme.colors[FOCUSED][COLOR_DARK].pixel;
		bot = theme.colors[FOCUSED][COLOR_LIGHT].pixel;
	} else {
		pressed = False;
		top = theme.colors[style][COLOR_LIGHT].pixel;
		bot = theme.colors[style][COLOR_DARK].pixel;
	}

	drawshadow(
		pix, 0, 0,
		config.titlewidth, config.titlewidth,
		style, pressed,
		config.shadowthickness
	);

	if (w > 0) {
		x = 4;
		y = config.titlewidth / 2 - 1;
		rects[0] = (XRectangle){.x = x, .y = y, .width = w, .height = 1};
		rects[1] = (XRectangle){.x = x, .y = y, .width = 1, .height = 3};
		val.foreground = (pressed) ? bot : top;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, rects, 2);
		rects[0] = (XRectangle){.x = x + 1, .y = y + 2, .width = w, .height = 1};
		rects[1] = (XRectangle){.x = x + w, .y = y, .width = 1, .height = 3};
		val.foreground = (pressed) ? top : bot;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, pix, gc, rects, 2);
	}
}

static void
draw_button_right(Pixmap pix, int style)
{
	XGCValues val;
	XPoint pts[9];
	unsigned long top, bot;
	int w;
	Bool pressed;

	w = (config.titlewidth - 11) / 2;
	if (style == PRESSED) {
		pressed = True;
		style = FOCUSED;
		top = theme.colors[FOCUSED][COLOR_DARK].pixel;
		bot = theme.colors[FOCUSED][COLOR_LIGHT].pixel;
	} else {
		pressed = False;
		top = theme.colors[style][COLOR_LIGHT].pixel;
		bot = theme.colors[style][COLOR_DARK].pixel;
	}

	drawshadow(
		pix, 0, 0,
		config.titlewidth, config.titlewidth,
		style, pressed,
		config.shadowthickness
	);

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
}

static void
reloadtheme(void)
{
	XRectangle rects[MAX_SHADOW_THICKNESS * 4];
	Pixmap pix;
	int wholesize;

	config.corner = config.borderwidth + config.titlewidth;
	config.divwidth = config.borderwidth;
	wm.minsize = config.corner * 2 + 10;
	if (config.borderwidth > 5)
		config.shadowthickness = MAX_SHADOW_THICKNESS;
	else
		config.shadowthickness = MIN_SHADOW_THICKNESS;
	wholesize = config.corner + config.shadowthickness;

	for (int style = 0; style < STYLE_LAST; style++) {
		int x, y;
		unsigned long top = theme.colors[style][COLOR_LIGHT].pixel;
		unsigned long bot = theme.colors[style][COLOR_DARK].pixel;

		/* background pixmap of left titlebar button */
		updatepixmap(
			&wm.decorations[style].btn_left,
			NULL, NULL,
			config.titlewidth, config.titlewidth
		);
		draw_button_left(wm.decorations[style].btn_left, style);

		/* background pixmap of right titlebar button */
		updatepixmap(
			&wm.decorations[style].btn_right,
			NULL, NULL,
			config.titlewidth, config.titlewidth
		);
		draw_button_right(wm.decorations[style].btn_right, style);

		/* background pixmap of horizontal (north and south) borders */
		updatepixmap(
			&wm.decorations[style].bar_horz,
			NULL, NULL, 1, config.borderwidth
		);
		drawshadow(
			wm.decorations[style].bar_horz,
			-config.shadowthickness, 0,
			config.borderwidth, config.borderwidth,
			style, False, config.shadowthickness
		);

		/* background pixmap of vertical (west and east) borders */
		updatepixmap(
			&wm.decorations[style].bar_vert,
			NULL, NULL, config.borderwidth, 1
		);
		drawshadow(
			wm.decorations[style].bar_vert,
			0, -config.shadowthickness,
			config.borderwidth, config.borderwidth,
			style, False, config.shadowthickness
		);

		/*
		 * Background pixmap of northwest corner.
		 * Corners' shadows are complex to draw, for corners are
		 * not a rectangle, but a 6-vertices polygon shaped like
		 * the uppercase greek letter Gamma.
		 */
		updatepixmap(
			&wm.decorations[style].corner_nw,
			NULL, NULL, wholesize, wholesize
		);
		x = y = 0;
		drawbackground(
			wm.decorations[style].corner_nw,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 2 + 0] = (XRectangle){.x = x + i, .y = y + 0, .width = 1,                     .height = config.corner - 1 - i};
			rects[i * 2 + 1] = (XRectangle){.x = x + 0, .y = y + i, .width = config.corner - 1 - i, .height = 1};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){ .foreground = top });
		XFillRectangles(
			dpy, wm.decorations[style].corner_nw,
			gc, rects, config.shadowthickness * 2
		);
		int i;
		for (i = 0; i < config.shadowthickness; i++) {
			rects[i * 4 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = 1,                         .height = config.titlewidth + 1 + i};
			rects[i * 4 + 1] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = config.titlewidth + 1 + i, .height = 1};
			rects[i * 4 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + i,                          .width = 1,                         .height = config.borderwidth - i};
			rects[i * 4 + 3] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i,      .width = config.borderwidth - i,    .height = 1};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){ .foreground = bot });
		XFillRectangles(
			dpy, wm.decorations[style].corner_nw,
			gc, rects, config.shadowthickness * 4
		);
		/*
		 * In addition to the corner's shadows, the shadows of
		 * the neighbouring borders are part of the corner.
		 * That is a optimization hack so we do not need to
		 * redraw the borders whenever the container resizes.
		 * The tip of each border are then "anchored" to the
		 * corners.
		 */
		drawshadow(
			wm.decorations[style].corner_nw,
			0, config.corner,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);
		drawshadow(
			wm.decorations[style].corner_nw,
			config.corner, 0,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);

		/* bottom left corner */
		updatepixmap(
			&wm.decorations[style].corner_sw,
			NULL, NULL, wholesize, wholesize
		);
		x = 0;
		y = config.shadowthickness;
		drawbackground(
			wm.decorations[style].corner_sw,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + i,                          .y = y + 0,                     .width = 1,                          .height = config.corner - 1 - i};
			rects[i * 3 + 1] = (XRectangle){.x = x + 0,                          .y = y + i,                     .width = config.borderwidth - 1 - i, .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.titlewidth + i, .width = config.titlewidth,          .height = 1};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){.foreground = top });
		XFillRectangles(
			dpy, wm.decorations[style].corner_sw,
			gc, rects, config.shadowthickness * 3
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + i,                     .width = 1,                 .height = config.titlewidth};
			rects[i * 3 + 1] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i, .width = config.corner - i, .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + config.titlewidth + i, .width = 1,                 .height = config.borderwidth - i};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){.foreground = bot});
		XFillRectangles(
			dpy, wm.decorations[style].corner_sw,
			gc, rects, config.shadowthickness * 3
		);
		drawshadow(
			wm.decorations[style].corner_sw,
			0, config.shadowthickness - config.borderwidth,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);
		drawshadow(
			wm.decorations[style].corner_sw,
			wholesize - config.shadowthickness,
			wholesize - config.borderwidth,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);

		/* top right corner */
		updatepixmap(
			&wm.decorations[style].corner_ne,
			NULL, NULL, wholesize, wholesize
		);
		x = config.shadowthickness;
		y = 0;
		drawbackground(
			wm.decorations[style].corner_ne,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + i,                     .y = y + 0,                          .width = 1,                     .height = config.borderwidth - 1 - i};
			rects[i * 3 + 1] = (XRectangle){.x = x + 0,                     .y = y + i,                          .width = config.corner - 1 - i, .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.borderwidth - 1 - i, .width = 1,                     .height = config.titlewidth};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){.foreground = top});
		XFillRectangles(
			dpy, wm.decorations[style].corner_ne,
			gc, rects, config.shadowthickness * 3
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                          .width = 1,                      .height = config.corner - i};
			rects[i * 3 + 1] = (XRectangle){.x = x + i,                     .y = y + config.borderwidth - 1 - i, .width = config.titlewidth,      .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.corner - 1 - i,      .width = config.borderwidth - i, .height = 1};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){.foreground = bot});
		XFillRectangles(
			dpy, wm.decorations[style].corner_ne,
			gc, rects, config.shadowthickness * 3
		);
		drawshadow(
			wm.decorations[style].corner_ne,
			wholesize - config.borderwidth, config.corner,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);
		drawshadow(
			wm.decorations[style].corner_ne,
			config.shadowthickness - config.borderwidth, 0,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);

		/* bottom right corner */
		updatepixmap(
			&wm.decorations[style].corner_se,
			NULL, NULL, wholesize, wholesize
		);
		x = config.shadowthickness;
		y = config.shadowthickness;
		drawbackground(
			wm.decorations[style].corner_se,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 4 + 0] = (XRectangle){.x = x + i,                     .y = y + config.titlewidth + i,  .width = 1,                              .height = config.borderwidth - 1 - i * 2};
			rects[i * 4 + 1] = (XRectangle){.x = x + config.titlewidth + i, .y = y + i,                      .width = config.borderwidth - 1 - i * 2, .height = 1};
			rects[i * 4 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + i,                      .width = 1,                              .height = config.titlewidth + 1};
			rects[i * 4 + 3] = (XRectangle){.x = x + i,                     .y = y + config.titlewidth + i,  .width = config.titlewidth + 1,          .height = 1};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){.foreground = top});
		XFillRectangles(
			dpy, wm.decorations[style].corner_se,
			gc, rects, config.shadowthickness * 4
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 2 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                     .width = 1,                      .height = config.corner - i};
			rects[i * 2 + 1] = (XRectangle){.x = x + i,                     .y = y + config.corner - 1 - i, .width = config.corner - i,      .height = 1};
		}
		XChangeGC(dpy, gc, GCForeground, &(XGCValues){.foreground = bot});
		XFillRectangles(
			dpy, wm.decorations[style].corner_se,
			gc, rects, config.shadowthickness * 2
		);
		drawshadow(
			wm.decorations[style].corner_se,
			wholesize - config.borderwidth,
			config.shadowthickness - config.borderwidth,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);
		drawshadow(
			wm.decorations[style].corner_se,
			config.shadowthickness - config.borderwidth,
			wholesize - config.borderwidth,
			config.borderwidth, config.borderwidth,
			style, 0, config.shadowthickness
		);
	}

	pix = XCreatePixmap(
		dpy,
		wm.dragwin,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		depth
	);
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
		FOCUSED, 0, config.shadowthickness
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

static Bool
set_theme(void)
{
	int i, j, error;

	error = 0;
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

Window
createframe(XRectangle geom)
{
	XSetWindowAttributes attrs = {
		.event_mask = MOUSE_EVENTS | StructureNotifyMask |
			SubstructureRedirectMask | FocusChangeMask,
	};

	if (config.sloppyfocus || config.sloppytiles)
		attrs.event_mask |= EnterWindowMask;
	return createwindow(root, geom, CWEventMask, &attrs);
}

Window
createdecoration(Window frame, XRectangle geom, Cursor cursor, int gravity)
{
	return createwindow(
		frame, geom, CWEventMask|CWCursor|CWWinGravity,
		&(XSetWindowAttributes){
			.event_mask = MOUSE_EVENTS,
			.cursor = cursor,
			.win_gravity = gravity,
		}
	);
}

void
updatepixmap(Pixmap *pix, int *pixw, int *pixh, int w, int h)
{
#define PIXMAP_INCREMENT        64

	if (*pix != None) {
		if (pixw != NULL && w <= *pixw)
			return;
		if (pixh != NULL && h <= *pixh)
		XFreePixmap(dpy, *pix);
	}
	if (pixw != NULL && w > *pixw)
		*pixw = w += PIXMAP_INCREMENT;
	if (pixh != NULL && h > *pixh)
		*pixh = h += PIXMAP_INCREMENT;
	*pix = XCreatePixmap(dpy, wm.checkwin, w, h, depth);
}

void
drawcommit(Pixmap pix, Window win)
{
	if (pix == None)
		return;
	XSetWindowBackgroundPixmap(dpy, win, pix);
	XClearWindow(dpy, win);
}

void
backgroundcommit(Window win, int style)
{
	XSetWindowBackgroundPixmap(dpy, win, None);
	XSetWindowBackground(dpy, win, theme.colors[style][COLOR_MID].pixel);
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

	if (text == NULL)
		return;
	if (pressed) {
		top = theme.colors[style][COLOR_DARK].pixel;
		bot = theme.colors[style][COLOR_LIGHT].pixel;
	} else {
		top = theme.colors[style][COLOR_LIGHT].pixel;
		bot = theme.colors[style][COLOR_DARK].pixel;
	}
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
	XRectangle rects[MAX_SHADOW_THICKNESS * 4];
	XftColor *decor;
	int partw, parth;
	int i;

	if (w <= 0 || h <= 0)
		return;

	decor = theme.colors[style];
	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	/* draw background */
	val.foreground = decor[COLOR_MID].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, w, h);

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 4 + 0] = (XRectangle){.x = i, .y = i, .width = 1, .height = h - 1 - i};
		rects[i * 4 + 1] = (XRectangle){.x = i, .y = i, .width = w - 1 - i, .height = 1};
		rects[i * 4 + 2] = (XRectangle){.x = w - config.borderwidth + i, .y = config.borderwidth - 1 - i, .width = 1, .height = parth + 2 * (i + 1)};
		rects[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 * (i + 1), .height = 1};
	}
	val.foreground = decor[COLOR_LIGHT].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, rects, config.shadowthickness * 4);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 4 + 0] = (XRectangle){.x = w - 1 - i, .y = i,         .width = 1,         .height = h - i * 2};
		rects[i * 4 + 1] = (XRectangle){.x = i,         .y = h - 1 - i, .width = w - i * 2, .height = 1};
		rects[i * 4 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = 1,                 .height = parth + 1 + i * 2};
		rects[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = partw + 1 + i * 2, .height = 1};
	}
	val.foreground = decor[COLOR_DARK].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, rects, config.shadowthickness * 4);
}

/* draw and fill rectangle */
void
drawbackground(Pixmap pix, int x, int y, int w, int h, int style)
{
	drawrectangle(pix, x, y, w, h, theme.colors[style][COLOR_MID].pixel);
}

/* draw rectangle shadows */
void
drawshadow(Pixmap pix, int x, int y, int w, int h, int style, int pressed, int thickness)
{
	XGCValues val;
	XRectangle rects[MAX_SHADOW_THICKNESS * 2];
	unsigned long top, bot;
	int i;

	if (w <= 0 || h <= 0)
		return;

	if (pressed) {
		top = theme.colors[style][COLOR_DARK].pixel;
		bot = theme.colors[style][COLOR_LIGHT].pixel;
	} else {
		top = theme.colors[style][COLOR_LIGHT].pixel;
		bot = theme.colors[style][COLOR_DARK].pixel;
	}

	drawbackground(pix, x, y, w, h, style);

	/* draw light shadow */
	for(i = 0; i < thickness; i++) {
		rects[i * 2]     = (XRectangle){.x = x + i, .y = y + i, .width = 1, .height = h - (i * 2 + 1)};
		rects[i * 2 + 1] = (XRectangle){.x = x + i, .y = y + i, .width = w - (i * 2 + 1), .height = 1};
	}
	val.foreground = top;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, rects, thickness * 2);

	/* draw dark shadow */
	for(i = 0; i < thickness; i++) {
		rects[i * 2]     = (XRectangle){.x = x + w - 1 - i, .y = y + i,         .width = 1,     .height = h - i * 2};
		rects[i * 2 + 1] = (XRectangle){.x = x + i,         .y = y + h - 1 - i, .width = w - i * 2, .height = 1};
	}
	val.foreground = bot;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, rects, thickness * 2);
}

void
drawprompt(Pixmap pix, int w, int h)
{
	XGCValues val;
	XRectangle rects[MAX_SHADOW_THICKNESS * 3];
	int i, partw, parth;

	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 3 + 0] = (XRectangle){.x = i,                          .y = i,                          .width = 1,                 .height = h - 1 - i};
		rects[i * 3 + 1] = (XRectangle){.x = w - config.borderwidth + i, .y = 0,                          .width = 1,                 .height = parth + config.borderwidth + i};
		rects[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 + i * 2, .height = 1};
	}
	val.foreground = theme.colors[FOCUSED][COLOR_LIGHT].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, rects, config.shadowthickness * 3);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 3 + 0] = (XRectangle){.x = w - 1 - i,                  .y = i,         .width = 1,         .height = h - i * 2};
		rects[i * 3 + 1] = (XRectangle){.x = i,                          .y = h - 1 - i, .width = w - i * 2, .height = 1};
		rects[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = i,         .width = 1,         .height = parth + config.borderwidth};
	}
	val.foreground = theme.colors[FOCUSED][COLOR_DARK].pixel;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, rects, config.shadowthickness * 3);
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

char *
getresource(XrmDatabase xdb, XrmClass *class, XrmName *name)
{
	XrmRepresentation tmp;
	XrmValue xval;

	if (xdb == NULL)
		return NULL;
	if (XrmQGetResource(xdb, name, class, &tmp, &xval))
		return xval.addr;
	return NULL;
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
			if ((n = strtol(value, NULL, 10)) >= 3 && n <= 16)
				config.borderwidth = n;
			break;
		case RES_TITLE_WIDTH:
			if ((n = strtol(value, NULL, 10)) >= 3 && n <= 32)
				config.titlewidth = n;
			break;
		case RES_DOCK_WIDTH:
			if ((n = strtol(value, NULL, 10)) >= 16 && n <= 256)
				config.dockwidth = n;
			break;
		case RES_DOCK_SPACE:
			if ((n = strtol(value, NULL, 10)) >= 16 && n <= 256)
				config.dockspace = n;
			break;
		case RES_DOCK_GRAVITY:
			config.dockgravity = value;
			break;
		case RES_NOTIFY_GAP:
			if ((n = strtol(value, NULL, 10)) >= 0 && n <= 64)
				config.notifgap = n;
			break;
		case RES_NOTIFY_GRAVITY:
			config.notifgravity = value;
			break;
		case RES_SNAP_PROXIMITY:
			if ((n = strtol(value, NULL, 10)) >= 0 && n < 64)
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

void
initdepth(void)
{
	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;
	int nitems;
	int i;

	visual = NULL;
	if ((infos = XGetVisualInfo(dpy, masks, &tpl, &nitems)) != NULL) {
		for (i = 0; i < nitems; i++) {
			fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
			if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
				depth = infos[i].depth;
				visual = infos[i].visual;
				colormap = XCreateColormap(dpy, root, visual, AllocNone);
				break;
			}
		}
		XFree(infos);
	}
	if (visual == NULL) {
		depth = DefaultDepth(dpy, screen);
		visual = DefaultVisual(dpy, screen);
		colormap = DefaultColormap(dpy, screen);
	}
}

void
inittheme(void)
{
	static struct {
		const char *class, *name;
	} resourceids[NRESOURCES] = {
#define X(res, s1, s2) [res] = { .class = s1, .name = s2, },
		RESOURCES
#undef  X
	};
	long n;
	int i;
	char *value;

	XrmInitialize();
	wm.application.class = XrmPermStringToQuark("Shod");
	wm.application.name = XrmPermStringToQuark("shod");
	wm.anyresource = XrmPermStringToQuark("?");
	for (i = 0; i < NRESOURCES; i++) {
		wm.resources[i].class = XrmPermStringToQuark(resourceids[i].class);
		wm.resources[i].name = XrmPermStringToQuark(resourceids[i].name);
	}
	gc = XCreateGC(
		dpy, wm.dragwin,
		GCFillStyle, &(XGCValues){.fill_style = FillSolid}
	);
	if (!set_theme())
		exit(EXIT_FAILURE);
	setresources(XResourceManagerString(dpy));
	if (xdb != NULL) {
		value = getresource(
			xdb,
			(XrmClass[]){
				wm.application.class,
				wm.resources[RES_NDESKTOPS].class,
				NULLQUARK,
			},
			(XrmName[]){
				wm.application.name,
				wm.resources[RES_NDESKTOPS].name,
				NULLQUARK,
			}
		);
		if (value != NULL && (n = strtol(value, NULL, 10)) > 0 && n < 100) {
			config.ndesktops = n;
		}
	}
}
