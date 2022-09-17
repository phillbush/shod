#include "shod.h"

/* decorate dock */
static void
dockdecorate(void)
{
	if (dock.pw != dock.w || dock.ph != dock.h || dock.pix == None)
		pixmapnew(&dock.pix, dock.win, dock.w, dock.h);
	dock.pw = dock.w;
	dock.ph = dock.h;
	drawdock(dock.pix, dock.w, dock.h);
	drawcommit(dock.pix, dock.win, dock.w, dock.h);
}

/* create dockapp */
static void
dockappnew(Window win, int w, int h, int dockpos, int ignoreunmap)
{
	struct Dockapp *dapp;
	struct Object *prev;

	dapp = emalloc(sizeof(*dapp));
	*dapp = (struct Dockapp){
		.obj.type = TYPE_DOCKAPP,
		.obj.win = win,
		.w = w,
		.h = h,
		.ignoreunmap = ignoreunmap,
		.dockpos = abs(dockpos),
		.extend = dockpos < 0,
	};
	TAILQ_FOREACH_REVERSE(prev, &dock.dappq, Queue, entry)
		if (((struct Dockapp *)prev)->dockpos <= dapp->dockpos)
			break;
	if (prev != NULL) {
		TAILQ_INSERT_AFTER(&dock.dappq, prev, (struct Object *)dapp, entry);
	} else {
		TAILQ_INSERT_HEAD(&dock.dappq, (struct Object *)dapp, entry);
	}
	switch (config.dockgravity[0]) {
	case 'N':
		dapp->slotsize = dapp->w / config.dockspace + (dapp->w % config.dockspace ? 1 : 0);
		dapp->h = min(config.dockwidth, dapp->h);
		break;
	case 'S':
		dapp->slotsize = dapp->w / config.dockspace + (dapp->w % config.dockspace ? 1 : 0);
		dapp->h = min(config.dockwidth, dapp->h);
		break;
	case 'W':
		dapp->slotsize = dapp->h / config.dockspace + (dapp->h % config.dockspace ? 1 : 0);
		dapp->w = min(config.dockwidth, dapp->w);
		break;
	case 'E':
	default:
		dapp->slotsize = dapp->h / config.dockspace + (dapp->h % config.dockspace ? 1 : 0);
		dapp->w = min(config.dockwidth, dapp->w);
		break;
	}
	dapp->slotsize *= config.dockspace;
}

/* update dock position; create it, if necessary */
void
dockupdateresizeable(void)
{
	struct Object *p;
	struct Dockapp *dapp;
	int size;

	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		switch (config.dockgravity[0]) {
		case 'N':
			dapp->x = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->w) / 2);
			dapp->y = DOCKBORDER + max(0, (config.dockwidth - dapp->h) / 2);
			break;
		case 'S':
			dapp->x = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->w) / 2);
			dapp->y = DOCKBORDER + max(0, (config.dockwidth - dapp->h) / 2);
			break;
		case 'W':
			dapp->x = DOCKBORDER + max(0, (config.dockwidth - dapp->w) / 2);
			dapp->y = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->h) / 2);
			break;
		case 'E':
		default:
			dapp->x = DOCKBORDER + max(0, (config.dockwidth - dapp->w) / 2);
			dapp->y = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->h) / 2);
			break;
		}
		size += dapp->slotsize;
	}
	if (size == 0) {
		XUnmapWindow(dpy, dock.win);
		dock.mapped = 0;
		return;
	}
	dock.mapped = 1;
	size += DOCKBORDER * 2;
	switch (config.dockgravity[0]) {
	case 'N':
		dock.h = config.dockwidth;
		dock.y = 0;
		break;
	case 'S':
		dock.h = config.dockwidth;
		dock.y = TAILQ_FIRST(&wm.monq)->mh - config.dockwidth;
		break;
	case 'W':
		dock.w = config.dockwidth;
		dock.x = 0;
		break;
	case 'E':
	default:
		dock.w = config.dockwidth;
		dock.x = TAILQ_FIRST(&wm.monq)->mw - config.dockwidth;
		dock.h = min(size, TAILQ_FIRST(&wm.monq)->mh);
		dock.y = TAILQ_FIRST(&wm.monq)->mh / 2 - size / 2;
		break;
	}
	if (config.dockgravity[0] == 'N' || config.dockgravity[0] == 'S') {
		switch (config.dockgravity[1]) {
		case 'F':
			dock.x = 0;
			dock.w = TAILQ_FIRST(&wm.monq)->mw;
			break;
		case 'W':
			dock.w = min(size, TAILQ_FIRST(&wm.monq)->mw);
			dock.x = 0;
			break;
		case 'E':
			dock.w = min(size, TAILQ_FIRST(&wm.monq)->mw);
			dock.x = TAILQ_FIRST(&wm.monq)->mw - size;
			break;
		default:
			dock.w = min(size, TAILQ_FIRST(&wm.monq)->mw);
			dock.x = TAILQ_FIRST(&wm.monq)->mw / 2 - size / 2;
			break;
		}
	} else if (config.dockgravity[0] != '\0') {
		switch (config.dockgravity[1]) {
		case 'F':
			dock.h = TAILQ_FIRST(&wm.monq)->mh;
			dock.y = 0;
			break;
		case 'N':
			dock.h = min(size, TAILQ_FIRST(&wm.monq)->mh);
			dock.y = 0;
			break;
		case 'S':
			dock.h = min(size, TAILQ_FIRST(&wm.monq)->mh);
			dock.y = TAILQ_FIRST(&wm.monq)->mh - size;
			break;
		default:
			dock.h = min(size, TAILQ_FIRST(&wm.monq)->mh);
			dock.y = TAILQ_FIRST(&wm.monq)->mh / 2 - size / 2;
			break;
		}
	}
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		XMoveResizeWindow(dpy, dapp->obj.win, dapp->x, dapp->y, dapp->w, dapp->h);
		winnotify(dapp->obj.win, dock.x + dapp->x, dock.y + dapp->y, dapp->w, dapp->h);
	}
}

/* update dock position; create it, if necessary */
void
dockupdatefull(void)
{
	struct Object *p;
	struct Dockapp *dapp;
	int part, nextend, size;
	int i, n;

	if (TAILQ_FIRST(&dock.dappq) == NULL) {
		XUnmapWindow(dpy, dock.win);
		dock.mapped = 0;
		return;
	}
	dock.mapped = 1;
	switch (config.dockgravity[0]) {
	case 'N':
		dock.x = 0;
		dock.y = 0;
		dock.w = TAILQ_FIRST(&wm.monq)->mw;
		dock.h = config.dockwidth;
		part = dock.w;
		break;
	case 'S':
		dock.x = 0;
		dock.y = TAILQ_FIRST(&wm.monq)->mh - config.dockwidth;
		dock.w = TAILQ_FIRST(&wm.monq)->mw;
		dock.h = config.dockwidth;
		part = dock.w;
		break;
	case 'W':
		dock.x = 0;
		dock.y = 0;
		dock.w = config.dockwidth;
		dock.h = TAILQ_FIRST(&wm.monq)->mh;
		part = dock.h;
		break;
	case 'E':
	default:
		dock.x = TAILQ_FIRST(&wm.monq)->mw - config.dockwidth;
		dock.y = 0;
		dock.w = config.dockwidth;
		dock.h = TAILQ_FIRST(&wm.monq)->mh;
		part = dock.h;
		break;
	}
	nextend = 0;
	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		if (dapp->extend) {
			nextend++;
		} else {
			size += dapp->slotsize;
		}
	}
	part = max(part - size, 1);
	if (nextend > 0)
		part /= nextend;
	i = 0;
	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		switch (config.dockgravity[0]) {
		case 'N':
			if (dapp->extend) {
				dapp->w = max(1, (i + 1) * part - i * part);
				n = dapp->w;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = size + max(0, (n - dapp->w) / 2);
			dapp->y = DOCKBORDER + max(0, (config.dockwidth - dapp->h) / 2);
			break;
		case 'S':
			if (dapp->extend) {
				dapp->w = max(1, (i + 1) * part - i * part);
				n = dapp->w;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = size + max(0, (n - dapp->w) / 2);
			dapp->y = DOCKBORDER + max(0, (config.dockwidth - dapp->h) / 2);
			break;
		case 'W':
			if (dapp->extend) {
				dapp->h = max(1, (i + 1) * part - i * part);
				n = dapp->h;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = DOCKBORDER + max(0, (config.dockwidth - dapp->w) / 2);
			dapp->y = size + max(0, (n - dapp->h) / 2);
			break;
		case 'E':
		default:
			if (dapp->extend) {
				dapp->h = max(1, (i + 1) * part - i * part);
				n = dapp->h;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = DOCKBORDER + max(0, (config.dockwidth - dapp->w) / 2);
			dapp->y = size + max(0, (n - dapp->h) / 2);
			break;
		}
		XMoveResizeWindow(dpy, dapp->obj.win, dapp->x, dapp->y, dapp->w, dapp->h);
		winnotify(dapp->obj.win, dock.x + dapp->x, dock.y + dapp->y, dapp->w, dapp->h);
		size += n;
	}
}

/* update dock position; create it, if necessary */
void
dockupdate(void)
{
	Window wins[2];

	if (config.dockgravity[0] != '\0' && (config.dockgravity[1] == 'F' || config.dockgravity[1] == 'f')) {
		dockupdatefull();
	} else {
		dockupdateresizeable();
	}
	dockdecorate();
	wins[0] = wm.docklayer;
	wins[1] = dock.win;
	XMoveResizeWindow(dpy, dock.win, dock.x, dock.y, dock.w, dock.h);
	XRestackWindows(dpy, wins, 2);
	XMapWindow(dpy, dock.win);
	XMapSubwindows(dpy, dock.win);
}

/* map dockapp window */
void
managedockapp(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int pos, int ignoreunmap)
{
	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	XReparentWindow(dpy, win, dock.win, 0, 0);
	dockappnew(win, rect.width, rect.height, pos, ignoreunmap);
	dockupdate();
	monupdatearea();
}

/* delete dockapp */
int
unmanagedockapp(struct Object *obj, int ignoreunmap)
{
	struct Dockapp *dapp;

	dapp = (struct Dockapp *)obj;
	if (ignoreunmap && dapp->ignoreunmap) {
		dapp->ignoreunmap--;
		return 0;
	}
	TAILQ_REMOVE(&dock.dappq, (struct Object *)dapp, entry);
	XReparentWindow(dpy, dapp->obj.win, root, 0, 0);
	free(dapp);
	dockupdate();
	monupdatearea();
	return 0;
}
