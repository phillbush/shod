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
		.dockpos = dockpos,
	};
	TAILQ_FOREACH_REVERSE(prev, &dock.dappq, Queue, entry)
		if (((struct Dockapp *)prev)->dockpos <= dockpos)
			break;
	if (prev != NULL) {
		TAILQ_INSERT_AFTER(&dock.dappq, prev, (struct Object *)dapp, entry);
	} else {
		TAILQ_INSERT_HEAD(&dock.dappq, (struct Object *)dapp, entry);
	}
}

/* update dock position; create it, if necessary */
void
dockupdate(void)
{
	struct Object *p;
	struct Dockapp *dapp;
	Window wins[2];
	int size;
	int n;

	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		switch (config.dockgravity[0]) {
		case 'N':
			dapp->x = DOCKBORDER + size;
			dapp->y = DOCKBORDER;
			n = dapp->w / config.dockspace + (dapp->w % config.dockspace ? 1 : 0);
			n *= config.dockspace;
			dapp->x += max(0, (n - dapp->w) / 2);
			dapp->y += max(0, (config.dockwidth - dapp->h) / 2);
			break;
		case 'S':
			dapp->x = DOCKBORDER + size;
			dapp->y = DOCKBORDER;
			n = dapp->w / config.dockspace + (dapp->w % config.dockspace ? 1 : 0);
			n *= config.dockspace;
			dapp->x += max(0, (n - dapp->w) / 2);
			dapp->y += max(0, (config.dockwidth - dapp->h) / 2);
			break;
		case 'W':
			dapp->x = DOCKBORDER;
			dapp->y = DOCKBORDER + size;
			n = dapp->h / config.dockspace + (dapp->h % config.dockspace ? 1 : 0);
			n *= config.dockspace;
			dapp->x += max(0, (config.dockwidth - dapp->w) / 2);
			dapp->y += max(0, (n - dapp->h) / 2);
			break;
		case 'E':
		default:
			dapp->y = DOCKBORDER + size;
			dapp->x = DOCKBORDER;
			n = dapp->h / config.dockspace + (dapp->h % config.dockspace ? 1 : 0);
			n *= config.dockspace;
			dapp->x += max(0, (config.dockwidth - dapp->w) / 2);
			dapp->y += max(0, (n - dapp->h) / 2);
			break;
		}
		size += n;
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
		XMoveWindow(dpy, dapp->obj.win, dapp->x, dapp->y);
		winnotify(dapp->obj.win, dock.x + dapp->x, dock.y + dapp->y, dapp->w, dapp->h);
	}
	dockdecorate();
	wins[0] = wm.layerwins[LAYER_DOCK];
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
