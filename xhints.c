#include "shod.h"

#define ISDUMMY(c)              ((c)->ncols == 0)

static void
clearprop(Atom prop)
{
	XChangeProperty(
		dpy, root,
		prop,
		XA_WINDOW, 32,
		PropModeReplace,
		NULL,
		0
	);
}

/* set frames of window */
void
ewmhsetframeextents(Window win, int b, int t)
{
	unsigned long data[4];

	data[0] = data[1] = data[3] = b;
	data[2] = b + t;
	XChangeProperty(dpy, win, atoms[_NET_FRAME_EXTENTS], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 4);
}

/* set icccm wmstate */
void
icccmwmstate(Window win, int state)
{
	long data[2];

	data[0] = state;
	data[1] = None;
	XChangeProperty(dpy, win, atoms[WM_STATE], atoms[WM_STATE], 32, PropModeReplace, (unsigned char *)&data, 2);
}

/* delete window state property */
void
icccmdeletestate(Window win)
{
	XDeleteProperty(dpy, win, atoms[WM_STATE]);
}

/* update tab title */
void
winupdatetitle(Window win, char **name)
{
	Atom properties[] = {atoms[_NET_WM_NAME], XA_WM_NAME};

	free(*name);
	for (size_t i = 0; i < LEN(properties); i++) {
		if (getprop(
			dpy, win, properties[i],
			AnyPropertyType, 8, 0,
			(void *)name
		) > 0)
			return;
		XFree(*name);
	}
	*name = NULL;
}

/* notify window of configuration changing */
void
winnotify(Window win, int x, int y, int w, int h)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.x = x;
	ce.y = y;
	ce.width = w;
	ce.height = h;
	ce.border_width = 0;
	ce.above = None;
	ce.override_redirect = False;
	ce.event = win;
	ce.window = win;
	XSendEvent(dpy, win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
mapwin(Window win)
{
	XMapWindow(dpy, win);
	icccmwmstate(win, NormalState);
}

void
unmapwin(Window win)
{
	XWindowAttributes attrs;

	if (!XGetWindowAttributes(dpy, win, &attrs))
		attrs.your_event_mask = 0;
	XSelectInput(dpy, win, PropertyChangeMask|FocusChangeMask);
	XUnmapWindow(dpy, win);
	XSelectInput(dpy, win, StructureNotifyMask|PropertyChangeMask|FocusChangeMask);
	icccmwmstate(win, IconicState);
}
