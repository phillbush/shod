#include "shod.h"

#define ISDUMMY(c)              ((c)->ncols == 0)

/* set current desktop hint */
void
ewmhsetcurrentdesktop(unsigned long n)
{
	XChangeProperty(dpy, root, atoms[_NET_CURRENT_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
}

/* set showing desktop hint */
void
ewmhsetshowingdesktop(int n)
{
	XChangeProperty(dpy, root, atoms[_NET_SHOWING_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
}

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

static void
setwinprop(Atom prop, Window wins[], int nwins)
{
	XChangeProperty(
		dpy, root,
		prop,
		XA_WINDOW, 32,
		PropModeReplace,
		(const unsigned char *)wins,
		nwins
	);
}

void
ewmhsetclients(void)
{
	struct Container *c;
	struct Object *l, *r, *t;
	Window *wins;
	int i;

	if (wm.nclients < 1) {
		clearprop(atoms[_NET_CLIENT_LIST]);
		clearprop(atoms[_NET_CLIENT_LIST_STACKING]);
		clearprop(atoms[_SHOD_CONTAINER_LIST]);
		clearprop(atoms[_SHOD_DOCK_LIST]);
		return;
	}
	i = wm.nclients;
	wins = ecalloc(wm.nclients, sizeof(*wins));
	TAILQ_FOREACH(c, &wm.stackq, raiseentry) {
		if (ISDUMMY(c))
			continue;
		TAILQ_FOREACH(l, &c->colq, entry) {
			struct Column *col = (struct Column *)l;
			if (col->selrow->seltab != NULL)
				wins[--i] = col->selrow->seltab->obj.win;
			TAILQ_FOREACH(r, &col->rowq, entry)
			TAILQ_FOREACH(t, &((struct Row *)r)->tabq, entry) {
				if ((struct Tab *)t == col->selrow->seltab)
					continue;
				wins[--i] = t->win;
			}
		}
	}
	setwinprop(atoms[_NET_CLIENT_LIST], wins, wm.nclients - i);
	setwinprop(atoms[_NET_CLIENT_LIST_STACKING], wins, wm.nclients - i);
	i = wm.nclients;
	TAILQ_FOREACH(c, &wm.stackq, raiseentry) {
		if (ISDUMMY(c))
			continue;
		wins[--i] = c->selcol->selrow->seltab->obj.win;
	}
	setwinprop(atoms[_SHOD_CONTAINER_LIST], wins, wm.nclients - i);
	free(wins);
}

/* set active window hint */
void
ewmhsetactivewindow(Window w)
{
	XChangeProperty(dpy, root, atoms[_NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
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

/* set state of windows */
void
ewmhsetstate(struct Container *c)
{
	struct Object *t;
	Atom data[9];
	int n = 0;

	if (c == NULL)
		return;
	if (c == wm.focused)
		data[n++] = atoms[_NET_WM_STATE_FOCUSED];
	if (c->state & FULLSCREEN)
		data[n++] = atoms[_NET_WM_STATE_FULLSCREEN];
	if (c->state & STICKY)
		data[n++] = atoms[_NET_WM_STATE_STICKY];
	if (c->state & SHADED)
		data[n++] = atoms[_NET_WM_STATE_SHADED];
	if (c->state & MINIMIZED)
		data[n++] = atoms[_NET_WM_STATE_HIDDEN];
	if (c->state & ABOVE)
		data[n++] = atoms[_NET_WM_STATE_ABOVE];
	if (c->state & BELOW)
		data[n++] = atoms[_NET_WM_STATE_BELOW];
	if (c->state & MAXIMIZED) {
		data[n++] = atoms[_NET_WM_STATE_MAXIMIZED_VERT];
		data[n++] = atoms[_NET_WM_STATE_MAXIMIZED_HORZ];
	}
	TAB_FOREACH_BEGIN(c, t){
		XChangeProperty(dpy, t->win, atoms[_NET_WM_STATE], XA_ATOM, 32, PropModeReplace, (unsigned char *)data, n);
	}TAB_FOREACH_END
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

/* set group of windows in client */
void
shodgrouptab(struct Container *c)
{
	struct Object *t;

	TAB_FOREACH_BEGIN(c, t){
		XChangeProperty(
			dpy, t->win,
			atoms[_SHOD_GROUP_TAB], XA_WINDOW, 32,
			PropModeReplace,
			(void *)&((struct Tab *)t)->row->seltab->obj.win, 1
		);
	}TAB_FOREACH_END
}

/* set group of windows in client */
void
shodgroupcontainer(struct Container *c)
{
	struct Object *t;

	TAB_FOREACH_BEGIN(c, t){
		XChangeProperty(dpy, t->win, atoms[_SHOD_GROUP_CONTAINER], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&c->selcol->selrow->seltab->obj.win, 1);
	}TAB_FOREACH_END
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
