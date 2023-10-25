#include "shod.h"

/* get window name */
static char *
getwinname(Window win)
{
	XTextProperty tprop;
	char **list = NULL;
	char *name = NULL;
	unsigned char *p = NULL;
	unsigned long size, dl;
	int di;
	Atom da;

	if (XGetWindowProperty(dpy, win, atoms[_NET_WM_NAME], 0L, NAMEMAXLEN, False, atoms[UTF8_STRING],
	                       &da, &di, &size, &dl, &p) == Success && p) {
		name = estrndup((char *)p, NAMEMAXLEN);
		XFree(p);
	} else if (XGetWMName(dpy, win, &tprop) &&
	           XmbTextPropertyToTextList(dpy, &tprop, &list, &di) == Success &&
	           di > 0 && list && *list) {
		name = estrndup(*list, NAMEMAXLEN);
		XFreeStringList(list);
		XFree(tprop.value);
	}
	return name;
}

/* check if given geometry is obscured by containers above it */
static int
isobscured(struct Container *c, struct Monitor *mon, int desk, int x, int y, int w, int h)
{
	x = max(x, mon->wx);
	y = max(y, mon->wy);
	w = min(x + w, mon->wx + mon->ww) - x;
	h = min(y + h, mon->wy + mon->wh) - y;
	if (config.disablehidden || c == NULL)
		return 0;
	if (w <= 0 || h <= 0)
		return 1;
	while ((c = TAILQ_PREV(c, ContainerQueue, raiseentry)) != NULL) {
		if (ISDUMMY(c) || !containerisvisible(c, mon, desk))
			continue;
		return isobscured(c, mon, desk, x, y, w, c->y - y) &&
		       isobscured(c, mon, desk, x, y, c->x - x, h) &&
		       isobscured(c, mon, desk, x, c->y + c->h, w, y + h - (c->y + c->h)) &&
		       isobscured(c, mon, desk, c->x + c->w, y, x + w - (c->x + c->w), h);
	}
	return 0;
}

/* set desktop for a given window */
void
ewmhsetdesktop(Window win, long d)
{
	XChangeProperty(dpy, win, atoms[_NET_WM_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&d, 1);
}

/* initialize ewmh hints */
void
ewmhinit(const char *wmname)
{
	/* set window and property that indicates that the wm is ewmh compliant */
	XChangeProperty(dpy, wm.checkwin, atoms[_NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wm.checkwin, 1);
	XChangeProperty(dpy, wm.checkwin, atoms[_NET_WM_NAME], atoms[UTF8_STRING], 8, PropModeReplace, (unsigned char *)wmname, strlen(wmname));
	XChangeProperty(dpy, root, atoms[_NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wm.checkwin, 1);

	/* set properties that the window manager supports */
	XChangeProperty(dpy, root, atoms[_NET_SUPPORTED], XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, NATOMS);
	XDeleteProperty(dpy, root, atoms[_NET_CLIENT_LIST]);

	/* set number of desktops */
	XChangeProperty(dpy, root, atoms[_NET_NUMBER_OF_DESKTOPS], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&config.ndesktops, 1);
}

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
prependprop(Atom prop, Window win)
{
	XChangeProperty(
		dpy, root,
		prop,
		XA_WINDOW, 32,
		PropModePrepend,
		(unsigned char *)&win,
		1
	);
}

void
ewmhsetclients(void)
{
	struct Container *c;
	struct Column *col;
	struct Row *row;
	struct Object *obj;
	int prevobscured;

	clearprop(atoms[_NET_CLIENT_LIST]);
	clearprop(atoms[_NET_CLIENT_LIST_STACKING]);
	clearprop(atoms[_SHOD_CONTAINER_LIST]);
	clearprop(atoms[_SHOD_DOCK_LIST]);
	TAILQ_FOREACH(c, &wm.stackq, raiseentry) {
		if (ISDUMMY(c))
			continue;
		prependprop(
			atoms[_SHOD_CONTAINER_LIST],
			c->selcol->selrow->seltab->obj.win
		);
		prevobscured = c->isobscured;
		if (!config.disablehidden) {
			c->isobscured = isobscured(
				c,
				c->mon, c->desk,
				c->x, c->y, c->w, c->h
			);
		}
		TAILQ_FOREACH(col, &c->colq, entry) {
			if (col->selrow->seltab != NULL) {
				prependprop(
					atoms[_NET_CLIENT_LIST],
					col->selrow->seltab->obj.win
				);
				prependprop(
					atoms[_NET_CLIENT_LIST_STACKING],
					col->selrow->seltab->obj.win
				);
			}
			TAILQ_FOREACH(row, &col->rowq, entry)
			TAILQ_FOREACH(obj, &row->tabq, entry) {
				if ((struct Tab *)obj == col->selrow->seltab)
					continue;
				prependprop(
					atoms[_NET_CLIENT_LIST],
					obj->win
				);
				prependprop(
					atoms[_NET_CLIENT_LIST_STACKING],
					obj->win
				);
			}
		}
		if (prevobscured != c->isobscured) {
			ewmhsetstate(c);
		}
	}
	TAILQ_FOREACH(obj, &wm.barq, entry) {
		prependprop(
			atoms[_SHOD_DOCK_LIST],
			obj->win
		);
	}
	if (!TAILQ_EMPTY(&dock.dappq)) {
		prependprop(
			atoms[_SHOD_DOCK_LIST],
			dock.obj.win
		);
	}
}

/* set active window hint */
void
ewmhsetactivewindow(Window w)
{
	XChangeProperty(dpy, root, atoms[_NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

/* set desktop for all windows in a container */
void
ewmhsetwmdesktop(struct Container *c)
{
	struct Object *t;
	unsigned long n;

	if (c->state & (STICKY|MINIMIZED))
		n = 0xFFFFFFFF;
	else
		n = (unsigned long)c->desk;
	TAB_FOREACH_BEGIN(c, t){
		ewmhsetdesktop(t->win, n);
	}TAB_FOREACH_END
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
	if (c->state & MINIMIZED || c->isobscured)
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
		XChangeProperty(dpy, t->win, atoms[_SHOD_GROUP_TAB], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&row->seltab->obj.win, 1);
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
	free(*name);
	*name = getwinname(win);
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

/* send a WM_DELETE message to client */
void
winclose(Window win)
{
	XEvent ev;

	ev.type = ClientMessage;
	ev.xclient.window = win;
	ev.xclient.message_type = atoms[WM_PROTOCOLS];
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = atoms[WM_DELETE_WINDOW];
	ev.xclient.data.l[1] = CurrentTime;

	/*
	 * communicate with the given Client, kindly telling it to
	 * close itself and terminate any associated processes using
	 * the WM_DELETE_WINDOW protocol
	 */
	XSendEvent(dpy, win, False, NoEventMask, &ev);
}
