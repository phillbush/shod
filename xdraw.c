#include <err.h>
#include <stdlib.h>

#include "shod.h"

static void
drawrectangle(Pixmap pix, int x, int y, int w, int h, unsigned long color)
{
	XChangeGC(
		dpy, wm.gc,
		GCForeground,
		&(XGCValues){ .foreground = color }
	);
	XFillRectangle(dpy, pix, wm.gc, x, y, w, h);
}

static Window
createwindow(Window parent, XRectangle geom, long mask, XSetWindowAttributes *attrs)
{
	XSetWindowAttributes new_attrs = {0};

	if (attrs == NULL)
		attrs = &new_attrs;
	mask |= CWEventMask|CWColormap|CWBackPixel|CWBorderPixel;
	attrs->event_mask |= MOUSE_EVENTS;
	attrs->colormap = colormap;
	attrs->border_pixel = BlackPixel(dpy, screen);
	attrs->background_pixel = BlackPixel(dpy, screen);
	return XCreateWindow(
		dpy, parent,
		geom.x, geom.y, geom.width, geom.height, 0,
		depth, InputOutput, visual, mask, attrs
	);
}

Window
createframe(XRectangle geom)
{
	XSetWindowAttributes attrs = {
		.event_mask = EnterWindowMask,
	};

	return createwindow(root, geom, CWEventMask, &attrs);
}

Window
createdecoration(Window frame, XRectangle geom, Cursor cursor, int gravity)
{
	return createwindow(
		frame, geom, CWCursor|CWWinGravity,
		&(XSetWindowAttributes){
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
		if (pixw != NULL && pixh != NULL && w <= *pixw && h <= *pixh)
			return;
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
	XSetWindowBackground(dpy, win, theme.colors[style][COLOR_BODY].pixel);
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
		color = &theme.colors[STYLE_OTHER][COLOR_LIGHT];
	else
		color = &theme.colors[style][COLOR_LIGHT];
	draw = XftDrawCreate(dpy, pix, visual, colormap);
	len = strlen(text);
	XftTextExtentsUtf8(dpy, theme.font, (FcChar8 *)text, len, &box);
	x = max(0, (w - box.width) / 2 + box.x);
	y = (config.titlewidth - theme.font->ascent) / 2 + theme.font->ascent;
	for (i = 3; drawlines && i < config.titlewidth - 3; i += 3) {
		val.foreground = top;
		XChangeGC(dpy, wm.gc, GCForeground, &val);
		XFillRectangle(dpy, pix, wm.gc, 4, i, x - 8, 1);
		XFillRectangle(dpy, pix, wm.gc, w - x + 2, i, x - 6, 1);
	}
	for (i = 4; drawlines && i < config.titlewidth - 2; i += 3) {
		val.foreground = bot;
		XChangeGC(dpy, wm.gc, GCForeground, &val);
		XFillRectangle(dpy, pix, wm.gc, 4, i, x - 8, 1);
		XFillRectangle(dpy, pix, wm.gc, w - x + 2, i, x - 6, 1);
	}
	XftDrawStringUtf8(draw, color, theme.font, x, y, (FcChar8 *)text, len);
	XftDrawDestroy(draw);
}

/* draw borders with shadows */
void
drawborders(Pixmap pix, int w, int h, int style)
{
	XGCValues val;
	XRectangle rects[config.shadowthickness * 4];
	XftColor *decor;
	int partw, parth;
	int i;

	if (w <= 0 || h <= 0)
		return;

	decor = theme.colors[style];
	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	/* draw background */
	val.foreground = decor[COLOR_BODY].pixel;
	XChangeGC(dpy, wm.gc, GCForeground, &val);
	XFillRectangle(dpy, pix, wm.gc, 0, 0, w, h);

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 4 + 0] = (XRectangle){.x = i, .y = i, .width = 1, .height = h - 1 - i};
		rects[i * 4 + 1] = (XRectangle){.x = i, .y = i, .width = w - 1 - i, .height = 1};
		rects[i * 4 + 2] = (XRectangle){.x = w - config.borderwidth + i, .y = config.borderwidth - 1 - i, .width = 1, .height = parth + 2 * (i + 1)};
		rects[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 * (i + 1), .height = 1};
	}
	val.foreground = decor[COLOR_LIGHT].pixel;
	XChangeGC(dpy, wm.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, wm.gc, rects, config.shadowthickness * 4);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 4 + 0] = (XRectangle){.x = w - 1 - i, .y = i,         .width = 1,         .height = h - i * 2};
		rects[i * 4 + 1] = (XRectangle){.x = i,         .y = h - 1 - i, .width = w - i * 2, .height = 1};
		rects[i * 4 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = 1,                 .height = parth + 1 + i * 2};
		rects[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = partw + 1 + i * 2, .height = 1};
	}
	val.foreground = decor[COLOR_DARK].pixel;
	XChangeGC(dpy, wm.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, wm.gc, rects, config.shadowthickness * 4);
}

void
drawbackground(Pixmap pix, int x, int y, int w, int h, int style)
{
	drawrectangle(pix, x, y, w, h, theme.colors[style][COLOR_BODY].pixel);
}

void
drawshadow(Pixmap pix, int x, int y, int w, int h, int style)
{
	XGCValues val;
	XRectangle rects[config.shadowthickness * 2];

	if (w <= 0 || h <= 0)
		return;

	drawbackground(pix, x, y, w, h, style);

	/* draw light shadow */
	for (int i = 0; i < config.shadowthickness; i++) {
		rects[i * 2]     = (XRectangle){.x = x + i, .y = y + i, .width = 1, .height = h - (i * 2 + 1)};
		rects[i * 2 + 1] = (XRectangle){.x = x + i, .y = y + i, .width = w - (i * 2 + 1), .height = 1};
	}
	XChangeGC(dpy, wm.gc, GCForeground, &(XGCValues){
		.foreground = theme.colors[style][COLOR_LIGHT].pixel,
	});
	XFillRectangles(dpy, pix, wm.gc, rects, config.shadowthickness * 2);

	/* draw dark shadow */
	for (int i = 0; i < config.shadowthickness; i++) {
		rects[i * 2]     = (XRectangle){.x = x + w - 1 - i, .y = y + i,         .width = 1,     .height = h - i * 2};
		rects[i * 2 + 1] = (XRectangle){.x = x + i,         .y = y + h - 1 - i, .width = w - i * 2, .height = 1};
	}
	XChangeGC(dpy, wm.gc, GCForeground, &(XGCValues){
		.foreground = theme.colors[style][COLOR_DARK].pixel,
	});
	XFillRectangles(dpy, pix, wm.gc, rects, config.shadowthickness * 2);
}

void
drawprompt(Pixmap pix, int w, int h)
{
	XGCValues val;
	XRectangle rects[config.shadowthickness * 3];
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
	XChangeGC(dpy, wm.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, wm.gc, rects, config.shadowthickness * 3);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 3 + 0] = (XRectangle){.x = w - 1 - i,                  .y = i,         .width = 1,         .height = h - i * 2};
		rects[i * 3 + 1] = (XRectangle){.x = i,                          .y = h - 1 - i, .width = w - i * 2, .height = 1};
		rects[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = i,         .width = 1,         .height = parth + config.borderwidth};
	}
	val.foreground = theme.colors[FOCUSED][COLOR_DARK].pixel;
	XChangeGC(dpy, wm.gc, GCForeground, &val);
	XFillRectangles(dpy, pix, wm.gc, rects, config.shadowthickness * 3);
}
