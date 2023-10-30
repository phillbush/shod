#include "shod.h"

void
dockstack(void)
{
	Window wins[2];

	if (wm.focused != NULL && wm.focused->state & FULLSCREEN &&
	    wm.focused->mon == TAILQ_FIRST(&wm.monq))
		wins[0] = wm.focused->frame;
	else if (dock.state & BELOW)
		wins[0] = wm.layers[LAYER_DESK].frame;
	else
		wins[0] = wm.layers[LAYER_DOCK].frame;
	wins[1] = dock.obj.win;
	XRestackWindows(dpy, wins, 2);
}

/* decorate dock */
void
dockdecorate(void)
{
	if (dock.pw != dock.w || dock.ph != dock.h || dock.pix == None)
		pixmapnew(&dock.pix, dock.obj.win, dock.w, dock.h);
	dock.pw = dock.w;
	dock.ph = dock.h;
	drawdock(dock.pix, dock.w, dock.h);
	drawcommit(dock.pix, dock.obj.win);
}

/* configure dockapp window */
void
dockappconfigure(struct Dockapp *dapp, unsigned int valuemask, XWindowChanges *wc)
{
	if (dapp == NULL)
		return;
	if (valuemask & CWWidth)
		dapp->w = wc->width;
	if (valuemask & CWHeight)
		dapp->h = wc->height;
	switch (config.dockgravity[0]) {
	case 'N':
	case 'S':
		if (dapp->state & SHRUNK)
			dapp->slotsize = dapp->w;
		else
			dapp->slotsize = (dapp->w / config.dockspace + (dapp->w % config.dockspace ? 1 : 0)) * config.dockspace;
		dapp->h = min(config.dockwidth, dapp->h);
		break;
	case 'W':
	case 'E':
	default:
		if (dapp->state & SHRUNK)
			dapp->slotsize = dapp->h;
		else
			dapp->slotsize = (dapp->h / config.dockspace + (dapp->h % config.dockspace ? 1 : 0)) * config.dockspace;
		dapp->w = min(config.dockwidth, dapp->w);
		break;
	}
}

static void
dockappinsert(struct Dockapp *dapp)
{
	struct Object *prev;

	if (dapp->dockpos == 0) {
		TAILQ_INSERT_TAIL(&dock.dappq, (struct Object *)dapp, entry);
	} else {
		TAILQ_FOREACH_REVERSE(prev, &dock.dappq, Queue, entry)
			if (((struct Dockapp *)prev)->dockpos <= dapp->dockpos)
				break;
		if (prev != NULL) {
			TAILQ_INSERT_AFTER(&dock.dappq, prev, (struct Object *)dapp, entry);
		} else {
			TAILQ_INSERT_HEAD(&dock.dappq, (struct Object *)dapp, entry);
		}
	}
	dockappconfigure(
		dapp,
		CWWidth | CWHeight,
		&(XWindowChanges){
			.width = dapp->w,
			.height = dapp->h,
		}
	);
}

/* create dockapp */
static void
dockappnew(Window win, int w, int h, int dockpos, int state)
{
	struct Dockapp *dapp;

	dapp = emalloc(sizeof(*dapp));
	*dapp = (struct Dockapp){
		.obj.class = dockapp_class,
		.obj.win = win,
		.x = 0,
		.y = 0,
		.w = w,
		.h = h,
		.dockpos = dockpos,
		.state = state,
	};
	dockappinsert(dapp);
}

/* compute dockapp position given its width or height */
static int
dockapppos(int pos)
{
	return max(0, config.dockwidth / 2 - pos / 2);
}

/* update dock position; create it, if necessary */
static void
dockupdateresizeable(void)
{
	struct Monitor *mon;
	struct Object *p;
	struct Dockapp *dapp;
	int size;

	mon = TAILQ_FIRST(&wm.monq);
	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		switch (config.dockgravity[0]) {
		case 'N':
			if (dapp->state & RESIZED)
				dapp->h = config.dockwidth;
			dapp->x = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->w) / 2);
			dapp->y = DOCKBORDER + dockapppos(dapp->h);
			break;
		case 'S':
			if (dapp->state & RESIZED)
				dapp->h = config.dockwidth;
			dapp->x = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->w) / 2);
			dapp->y = DOCKBORDER + dockapppos(dapp->h);
			break;
		case 'W':
			if (dapp->state & RESIZED)
				dapp->w = config.dockwidth;
			dapp->x = DOCKBORDER + dockapppos(dapp->w);
			dapp->y = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->h) / 2);
			break;
		case 'E':
		default:
			if (dapp->state & RESIZED)
				dapp->w = config.dockwidth;
			dapp->x = DOCKBORDER + dockapppos(dapp->w);
			dapp->y = DOCKBORDER + size + max(0, (dapp->slotsize - dapp->h) / 2);
			break;
		}
		size += dapp->slotsize;
	}
	if (size == 0) {
		XUnmapWindow(dpy, dock.obj.win);
		return;
	}
	size += DOCKBORDER * 2;
	switch (config.dockgravity[0]) {
	case 'N':
		dock.h = config.dockwidth;
		dock.x = mon->mx;
		dock.y = mon->my;
		break;
	case 'S':
		dock.h = config.dockwidth;
		dock.x = mon->mx;
		dock.y = mon->my + mon->mh - config.dockwidth;
		break;
	case 'W':
		dock.w = config.dockwidth;
		dock.x = mon->mx;
		dock.y = mon->my;
		break;
	case 'E':
	default:
		dock.w = config.dockwidth;
		dock.x = mon->mx + mon->mw - config.dockwidth;
		dock.h = min(size, mon->mh);
		dock.y = mon->my + mon->mh / 2 - size / 2;
		break;
	}
	if (config.dockgravity[0] == 'N' || config.dockgravity[0] == 'S') {
		switch (config.dockgravity[1]) {
		case 'F':
			dock.x = mon->mx;
			dock.w = mon->mw;
			break;
		case 'W':
			dock.w = min(size, mon->mw);
			dock.x = mon->mx;
			break;
		case 'E':
			dock.w = min(size, mon->mw);
			dock.x = mon->mx + mon->mw - size;
			break;
		default:
			dock.w = min(size, mon->mw);
			dock.x = mon->mx + mon->mw / 2 - size / 2;
			break;
		}
	} else if (config.dockgravity[0] != '\0') {
		switch (config.dockgravity[1]) {
		case 'F':
			dock.h = mon->mh;
			dock.y = mon->my;
			break;
		case 'N':
			dock.h = min(size, mon->mh);
			dock.y = mon->my;
			break;
		case 'S':
			dock.h = min(size, mon->mh);
			dock.y = mon->my + mon->mh - size;
			break;
		default:
			dock.h = min(size, mon->mh);
			dock.y = mon->my + mon->mh / 2 - size / 2;
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
static void
dockupdatefull(void)
{
	struct Object *p;
	struct Monitor *mon;
	struct Dockapp *dapp;
	int part, nextend, size;
	int i, n;

	mon = TAILQ_FIRST(&wm.monq);
	switch (config.dockgravity[0]) {
	case 'N':
		dock.x = mon->mx;
		dock.y = mon->my;
		dock.w = mon->mw;
		dock.h = config.dockwidth;
		part = dock.w;
		break;
	case 'S':
		dock.x = mon->mx;
		dock.y = mon->my + mon->mh - config.dockwidth;
		dock.w = mon->mw;
		dock.h = config.dockwidth;
		part = dock.w;
		break;
	case 'W':
		dock.x = mon->mx;
		dock.y = mon->my;
		dock.w = config.dockwidth;
		dock.h = mon->mh;
		part = dock.h;
		break;
	case 'E':
	default:
		dock.x = mon->mx + mon->mw - config.dockwidth;
		dock.y = mon->my;
		dock.w = config.dockwidth;
		dock.h = mon->mh;
		part = dock.h;
		break;
	}
	nextend = 0;
	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = (struct Dockapp *)p;
		if (dapp->state & EXTEND) {
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
			if (dapp->state & RESIZED)
				dapp->h = config.dockwidth - DOCKBORDER;
			if (dapp->state & EXTEND) {
				dapp->w = max(1, (i + 1) * part - i * part);
				n = dapp->w;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = size + max(0, (n - dapp->w) / 2);
			dapp->y = DOCKBORDER + dockapppos(dapp->h);
			break;
		case 'S':
			if (dapp->state & RESIZED)
				dapp->h = config.dockwidth - DOCKBORDER;
			if (dapp->state & EXTEND) {
				dapp->w = max(1, (i + 1) * part - i * part);
				n = dapp->w;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = size + max(0, (n - dapp->w) / 2);
			dapp->y = DOCKBORDER + dockapppos(dapp->h);
			break;
		case 'W':
			if (dapp->state & RESIZED)
				dapp->w = config.dockwidth - DOCKBORDER;
			if (dapp->state & EXTEND) {
				dapp->h = max(1, (i + 1) * part - i * part);
				n = dapp->h;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = DOCKBORDER + dockapppos(dapp->w);
			dapp->y = size + max(0, (n - dapp->h) / 2);
			break;
		case 'E':
		default:
			if (dapp->state & RESIZED)
				dapp->w = config.dockwidth - DOCKBORDER;
			if (dapp->state & EXTEND) {
				dapp->h = max(1, (i + 1) * part - i * part);
				n = dapp->h;
			} else {
				n = dapp->slotsize;
			}
			dapp->x = DOCKBORDER + dockapppos(dapp->w);
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
	if (TAILQ_EMPTY(&dock.dappq)) {
		XUnmapWindow(dpy, dock.obj.win);
		goto done;
	}
	if (config.dockgravity[0] != '\0' &&
	    (config.dockgravity[1] == 'F' || config.dockgravity[1] == 'f'))
		dockupdatefull();
	else
		dockupdateresizeable();
	dockdecorate();
	XMoveResizeWindow(dpy, dock.obj.win, dock.x, dock.y, dock.w, dock.h);
	dockstack();
	if (dock.state & MINIMIZED)
		XUnmapWindow(dpy, dock.obj.win);
	else
		XMapWindow(dpy, dock.obj.win);
	XMapSubwindows(dpy, dock.obj.win);
done:
	shoddocks();
}

static void
manage(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	XReparentWindow(dpy, win, dock.obj.win, 0, 0);
	dockappnew(win, rect.width, rect.height, rect.x, state);
	dockupdate();
	monupdatearea();
}

static void
unmanage(struct Object *obj)
{
	struct Dockapp *dapp;

	dapp = (struct Dockapp *)obj;
	TAILQ_REMOVE(&dock.dappq, (struct Object *)dapp, entry);
	XReparentWindow(dpy, dapp->obj.win, root, 0, 0);
	free(dapp);
	dockupdate();
	monupdatearea();
}

void
dockreset(void)
{
	struct Queue dappq;
	struct Object *obj;
	struct Dockapp *dapp;
	Window win, dummyw;
	struct Tab *dummyt;
	XRectangle rect;
	enum State state;
	int desk;

	TAILQ_INIT(&dappq);
	while ((obj = TAILQ_FIRST(&dock.dappq)) != NULL) {
		TAILQ_REMOVE(&dock.dappq, obj, entry);
		TAILQ_INSERT_TAIL(&dappq, obj, entry);
	}
	while ((obj = TAILQ_FIRST(&dappq)) != NULL) {
		TAILQ_REMOVE(&dappq, obj, entry);
		win = obj->win;
		dapp = (struct Dockapp *)obj;
		if (getwinclass(win, &dummyw, &dummyt, &state, &rect, &desk) == dockapp_class) {
			if (rect.x > 0) {
				dapp->dockpos = rect.x;
			}
			if (state != 0) {
				dapp->state = state;
			}
		}
		dockappinsert(dapp);
	}
	dockupdate();
}

static void
changestate(struct Object *obj, enum State mask, int set)
{
	enum State states[] = {
		ABOVE,
		BELOW,
		MAXIMIZED,
		MINIMIZED,
	};
	enum State state;
	size_t i;

	(void)obj;
	for (i = 0; i < LEN(states); i++) {
		state = states[i];
		if (!(mask & state))
			continue;       /* state change not requested */
		if (set == REMOVE && !(dock.state & state))
			continue;       /* state already unset */
		if (set == ADD    && !(dock.state & state))
			continue;       /* state already set */
		dock.state ^= state;
	}
	dockupdate();
	monupdatearea();
}

struct Class *dock_class = &(struct Class){
	.type           = TYPE_DOCK,
	.setstate       = &changestate,
	.manage         = NULL,
	.unmanage       = NULL,
};

struct Class *dockapp_class = &(struct Class){
	.type           = TYPE_DOCKAPP,
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
};
