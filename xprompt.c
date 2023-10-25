#include "shod.h"

/* calculate position and size of prompt window and the size of its frame */
static void
promptcalcgeom(int *x, int *y, int *w, int *h, int *fw, int *fh)
{
	*w = min(*w, wm.selmon->ww - config.borderwidth * 2);
	*h = min(*h, wm.selmon->wh - config.borderwidth);
	*x = wm.selmon->wx + (wm.selmon->ww - *w) / 2 - config.borderwidth;
	*y = wm.selmon->wy;
	*fw = *w + config.borderwidth * 2;
	*fh = *h + config.borderwidth;
}

/* check if event is related to the prompt or its frame */
static Bool
promptvalidevent(Display *dpy, XEvent *ev, XPointer arg)
{
	Window win;

	(void)dpy;
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
		XFreePixmap(dpy, *pix);
	*pix = XCreatePixmap(dpy, frame, w, h, depth);
	drawbackground(*pix, 0, 0, w, h, FOCUSED);
	drawprompt(*pix, w, h);
	drawcommit(*pix, frame);
}

/* map prompt, give it focus, wait for it to close, then revert focus to previously focused window */
void
manageprompt(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int state)
{
	Window frame;                           /* prompt frame */
	Pixmap pix;                             /* pixmap to draw the frame */
	XEvent ev;
	int x, y, w, h, fw, fh;

	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)state;
	w = rect.width;
	h = rect.height;
	promptcalcgeom(&x, &y, &w, &h, &fw, &fh);
	frame = XCreateWindow(dpy, root, x, y, fw, fh, 0, depth, CopyFromParent, visual, clientmask, &clientswa);
	pix = None;
	XReparentWindow(dpy, win, frame, config.borderwidth, 0);
	XMapWindow(dpy, win);
	XMapWindow(dpy, frame);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	promptdecorate(frame, &pix, fw, fh);
	while (!XIfEvent(dpy, &ev, promptvalidevent, (XPointer)&win)) {
		switch (ev.type) {
		case DestroyNotify:
		case UnmapNotify:
			goto done;
			break;
		case ConfigureRequest:
			w = ev.xconfigurerequest.width;
			h = ev.xconfigurerequest.height;
			promptcalcgeom(&x, &y, &w, &h, &fw, &fh);
			XMoveResizeWindow(dpy, frame, x, y, fw, fh);
			XMoveResizeWindow(dpy, win, config.borderwidth, 0, w, h);
			promptdecorate(frame, &pix, fw, fh);
			break;
		case ButtonPress:
			if (ev.xbutton.window != win && ev.xbutton.window != frame)
				winclose(win);
			XAllowEvents(dpy, ReplayPointer, CurrentTime);
			break;
		}
	}
done:
	XReparentWindow(dpy, win, root, 0, 0);
	XDestroyWindow(dpy, frame);
	if (wm.focused) {
		tabfocus(wm.focused->selcol->selrow->seltab, 0);
	} else {
		tabfocus(NULL, 0);
	}
}

int
unmanageprompt(struct Object *obj)
{
	(void)obj;
	return 0;
}
