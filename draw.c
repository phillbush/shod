#include <stdlib.h>

#include "shod.h"

static void
drawrectangle(Pixmap pix, int x, int y, int w, int h, unsigned long color)
{
	XChangeGC(
		wm.display, wm.gc,
		GCForeground,
		&(XGCValues){ .foreground = color }
	);
	XFillRectangle(wm.display, pix, wm.gc, x, y, w, h);
}

Window
createwindow(Window parent, XRectangle geom, long mask, XSetWindowAttributes *attrs)
{
	XSetWindowAttributes new_attrs = {0};

	if (attrs == NULL)
		attrs = &new_attrs;
	mask |= CWEventMask|CWColormap|CWBackPixel|CWBorderPixel;
	attrs->event_mask |= MOUSE_EVENTS;
	attrs->colormap = wm.colormap;
	attrs->border_pixel = BlackPixel(wm.display, wm.screen);
	attrs->background_pixel = BlackPixel(wm.display, wm.screen);
	return XCreateWindow(
		wm.display, parent,
		geom.x, geom.y, geom.width, geom.height, 0,
		wm.depth, InputOutput, wm.visual, mask, attrs
	);
}

Window
createframe(XRectangle geom)
{
	Window frame;

	frame = createwindow(
		wm.rootwin, geom,
		CWEventMask, &(XSetWindowAttributes){
			.event_mask = EnterWindowMask | SubstructureRedirectMask,
		}
	);
	XGrabButton(
		wm.display, AnyButton, AnyModifier,
		frame, False, MOUSE_EVENTS,
		GrabModeSync, GrabModeAsync, None, None
	);
	return frame;
}

Window
createdecoration(Window frame, XRectangle geom, Cursor cursor, int gravity)
{
	return createwindow(
		frame, geom, CWCursor|CWWinGravity|CWBitGravity,
		&(XSetWindowAttributes){
			.cursor = cursor,
			.win_gravity = gravity,
			.bit_gravity = gravity,
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
		XFreePixmap(wm.display, *pix);
	}
	if (pixw != NULL && w > *pixw)
		*pixw = w += PIXMAP_INCREMENT;
	if (pixh != NULL && h > *pixh)
		*pixh = h += PIXMAP_INCREMENT;
	*pix = XCreatePixmap(wm.display, wm.checkwin, w, h, wm.depth);
}

void
drawcommit(Pixmap pix, Window win)
{
	if (pix == None)
		return;
	XSetWindowBackgroundPixmap(wm.display, win, pix);
	XClearWindow(wm.display, win);
}

void
backgroundcommit(Window win, int style)
{
	XSetWindowBackgroundPixmap(wm.display, win, None);
	XSetWindowBackground(wm.display, win, wm.theme.colors[style][COLOR_BODY].pixel);
	XClearWindow(wm.display, win);
}

static void
draw_titlebar_lines(Drawable pix, int width, int style, Bool pressed)
{
	unsigned int top, bot;

	if (pressed) {
		top = wm.theme.colors[style][COLOR_DARK].pixel;
		bot = wm.theme.colors[style][COLOR_LIGHT].pixel;
	} else {
		top = wm.theme.colors[style][COLOR_LIGHT].pixel;
		bot = wm.theme.colors[style][COLOR_DARK].pixel;
	}
	for (int i = config.shadowthickness+1; i < config.titlewidth - config.shadowthickness-1; i += config.shadowthickness+1) {
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){
			.foreground = top,
		});
		XFillRectangle(
			wm.display, pix, wm.gc,
			config.shadowthickness*2, i,
			width - config.shadowthickness*2, 1
		);
	}
	for (int i = config.shadowthickness*2; i < config.titlewidth - config.shadowthickness; i += config.shadowthickness+1) {
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){
			.foreground = bot,
		});
		XFillRectangle(
			wm.display, pix, wm.gc,
			config.shadowthickness*2, i,
			width - config.shadowthickness*2, 1
		);
	}
}

void
drawtitle(Drawable pix, const char *text, int w, int drawlines, int style, int pressed, int active)
{
	XGlyphInfo box;
	XftColor *color;
	XftDraw *draw;
	size_t len;
	int x, y;

	if (text == NULL)
		return;
	if (active)
		color = &wm.theme.colors[STYLE_OTHER][COLOR_LIGHT];
	else
		color = &wm.theme.colors[style][COLOR_LIGHT];
	drawshadow(pix, 0, 0, w, config.titlewidth, style);
	draw = XftDrawCreate(wm.display, pix, wm.visual, wm.colormap);
	len = strlen(text);
	XftTextExtentsUtf8(wm.display, wm.theme.font, (FcChar8 *)text, len, &box);
	w -= config.titlewidth; /* ignore close button's size to draw title */
	x = max(0, (w - box.width) / 2);
	y = (config.titlewidth - wm.theme.font->height) / 2 + wm.theme.font->ascent;
	if (drawlines) {
		draw_titlebar_lines(pix, w, style, pressed);
		drawbackground(
			pix, x - config.shadowthickness*2,
			config.shadowthickness,
			box.width + config.shadowthickness*4,
			config.titlewidth - config.shadowthickness*2,
			style
		);
	}
	XftDrawStringUtf8(draw, color, wm.theme.font, x, y, (FcChar8 *)text, len);
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

	decor = wm.theme.colors[style];
	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	/* draw background */
	val.foreground = decor[COLOR_BODY].pixel;
	XChangeGC(wm.display, wm.gc, GCForeground, &val);
	XFillRectangle(wm.display, pix, wm.gc, 0, 0, w, h);

	/* draw light shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 4 + 0] = (XRectangle){.x = i, .y = i, .width = 1, .height = h - 1 - i};
		rects[i * 4 + 1] = (XRectangle){.x = i, .y = i, .width = w - 1 - i, .height = 1};
		rects[i * 4 + 2] = (XRectangle){.x = w - config.borderwidth + i, .y = config.borderwidth - 1 - i, .width = 1, .height = parth + 2 * (i + 1)};
		rects[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = h - config.borderwidth + i, .width = partw + 2 * (i + 1), .height = 1};
	}
	val.foreground = decor[COLOR_LIGHT].pixel;
	XChangeGC(wm.display, wm.gc, GCForeground, &val);
	XFillRectangles(wm.display, pix, wm.gc, rects, config.shadowthickness * 4);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 4 + 0] = (XRectangle){.x = w - 1 - i, .y = i,         .width = 1,         .height = h - i * 2};
		rects[i * 4 + 1] = (XRectangle){.x = i,         .y = h - 1 - i, .width = w - i * 2, .height = 1};
		rects[i * 4 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = 1,                 .height = parth + 1 + i * 2};
		rects[i * 4 + 3] = (XRectangle){.x = config.borderwidth - 1 - i, .y = config.borderwidth - 1 - i, .width = partw + 1 + i * 2, .height = 1};
	}
	val.foreground = decor[COLOR_DARK].pixel;
	XChangeGC(wm.display, wm.gc, GCForeground, &val);
	XFillRectangles(wm.display, pix, wm.gc, rects, config.shadowthickness * 4);
}

void
drawbackground(Pixmap pix, int x, int y, int w, int h, int style)
{
	drawrectangle(pix, x, y, w, h, wm.theme.colors[style][COLOR_BODY].pixel);
}

void
drawshadow(Pixmap pix, int x, int y, int w, int h, int style)
{
	XRectangle rects[config.shadowthickness * 2];

	if (w <= 0 || h <= 0)
		return;

	drawbackground(pix, x, y, w, h, style);

	/* draw light shadow */
	for (int i = 0; i < config.shadowthickness; i++) {
		rects[i * 2]     = (XRectangle){.x = x + i, .y = y + i, .width = 1, .height = h - (i * 2 + 1)};
		rects[i * 2 + 1] = (XRectangle){.x = x + i, .y = y + i, .width = w - (i * 2 + 1), .height = 1};
	}
	XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){
		.foreground = wm.theme.colors[style][COLOR_LIGHT].pixel,
	});
	XFillRectangles(wm.display, pix, wm.gc, rects, config.shadowthickness * 2);

	/* draw dark shadow */
	for (int i = 0; i < config.shadowthickness; i++) {
		rects[i * 2]     = (XRectangle){.x = x + w - 1 - i, .y = y + i,         .width = 1,     .height = h - i * 2};
		rects[i * 2 + 1] = (XRectangle){.x = x + i,         .y = y + h - 1 - i, .width = w - i * 2, .height = 1};
	}
	XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){
		.foreground = wm.theme.colors[style][COLOR_DARK].pixel,
	});
	XFillRectangles(wm.display, pix, wm.gc, rects, config.shadowthickness * 2);
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
	val.foreground = wm.theme.colors[FOCUSED][COLOR_LIGHT].pixel;
	XChangeGC(wm.display, wm.gc, GCForeground, &val);
	XFillRectangles(wm.display, pix, wm.gc, rects, config.shadowthickness * 3);

	/* draw dark shadow */
	for (i = 0; i < config.shadowthickness; i++) {
		rects[i * 3 + 0] = (XRectangle){.x = w - 1 - i,                  .y = i,         .width = 1,         .height = h - i * 2};
		rects[i * 3 + 1] = (XRectangle){.x = i,                          .y = h - 1 - i, .width = w - i * 2, .height = 1};
		rects[i * 3 + 2] = (XRectangle){.x = config.borderwidth - 1 - i, .y = i,         .width = 1,         .height = parth + config.borderwidth};
	}
	val.foreground = wm.theme.colors[FOCUSED][COLOR_DARK].pixel;
	XChangeGC(wm.display, wm.gc, GCForeground, &val);
	XFillRectangles(wm.display, pix, wm.gc, rects, config.shadowthickness * 3);
}
