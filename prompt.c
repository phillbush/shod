#include "shod.h"

/* calculate position and size of prompt window and the size of its frame */
static void
promptcalcgeom(int *x, int *y, int *w, int *h, int *fw, int *fh)
{
	*w = min(*w, wm.selmon->window_area.width - config.borderwidth * 2);
	*h = min(*h, wm.selmon->window_area.height - config.borderwidth);
	*x = wm.selmon->window_area.x + (wm.selmon->window_area.width - *w) / 2 - config.borderwidth;
	*y = wm.selmon->window_area.y;
	*fw = *w + config.borderwidth * 2;
	*fh = *h + config.borderwidth;
}

/* check if event is related to the prompt or its frame */
static Bool
promptvalidevent(Display *display, XEvent *ev, XPointer arg)
{
	Window win;

	(void)display;
	win = *(Window *)arg;
	switch(ev->type) {
	case DestroyNotify:
		if (ev->xdestroywindow.window == win)
			return True;
		break;
	case UnmapNotify:
		if (ev->xunmap.window == win)
			return True;
		break;
	case ConfigureRequest:
		if (ev->xconfigurerequest.window == win)
			return True;
		break;
	case ButtonPress:
		return True;
	}
	return False;
}

/* decorate prompt frame */
static void
promptdecorate(Window frame, Pixmap *pix, int w, int h)
{
	if (*pix != None)
		XFreePixmap(wm.display, *pix);
	*pix = XCreatePixmap(wm.display, frame, w, h, wm.depth);
	drawbackground(*pix, 0, 0, w, h, FOCUSED);
	drawprompt(*pix, w, h);
	drawcommit(*pix, frame);
}

static void
manage(struct Object *tab, struct Monitor *mon, int desk, Window win,
       Window leader, XRectangle rect, enum State state)
{
	Window frame;                           /* prompt frame */
	Pixmap pix;                             /* pixmap to draw the frame */
	XEvent ev;
	int x, y, w, h, fw, fh;

	/*
	 * Map prompt, give it focus, wait for it to close, then revert
	 * focus to previously focused window.
	 */
	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)state;
	w = rect.width;
	h = rect.height;
	promptcalcgeom(&x, &y, &w, &h, &fw, &fh);
	frame = createframe((XRectangle){x, y, fw, fh});
	pix = None;
	XReparentWindow(wm.display, win, frame, config.borderwidth, 0);
	XMapWindow(wm.display, win);
	XMapWindow(wm.display, frame);
	XSetInputFocus(wm.display, win, RevertToPointerRoot, CurrentTime);
	promptdecorate(frame, &pix, fw, fh);
	while (!XIfEvent(wm.display, &ev, promptvalidevent, (XPointer)&win)) {
		switch (ev.type) {
		case DestroyNotify:
		case UnmapNotify:
			goto done;
			break;
		case ConfigureRequest:
			w = ev.xconfigurerequest.width;
			h = ev.xconfigurerequest.height;
			promptcalcgeom(&x, &y, &w, &h, &fw, &fh);
			XMoveResizeWindow(wm.display, frame, x, y, fw, fh);
			XMoveResizeWindow(wm.display, win, config.borderwidth, 0, w, h);
			promptdecorate(frame, &pix, fw, fh);
			break;
		case ButtonPress:
			if (ev.xbutton.window != win && ev.xbutton.window != frame)
				window_close(wm.display, win);
			XAllowEvents(wm.display, ReplayPointer, CurrentTime);
			break;
		}
	}
done:
	XReparentWindow(wm.display, win, wm.rootwin, 0, 0);
	XDestroyWindow(wm.display, frame);
	focusnext(wm.selmon, wm.selmon->seldesk);
}

struct Class prompt_class = {
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = NULL,
};
