#include "shod.h"

#define DOCKBORDER 1

struct Dock {
	struct Object obj;

	struct Queue dappq;
	Pixmap pix;                     /* dock pixmap */
	int x, y, w, h;                 /* dock geometry */
	int pw, ph;                     /* dock pixmap size */
	enum State state;
};

struct Dockapp {
	struct Object obj;
	int x, y, w, h;                 /* dockapp position and size */
	int ignoreunmap;                /* number of unmap requests to ignore */
	int dockpos;                    /* position of the dockapp in the dock */
	int state;                      /* dockapp state */
	int slotsize;                   /* size of the slot the dockapp is in */
};

static struct Dock dock;

static void
restack(void)
{
	Window wins[2];

	if (focused_is_fullscreen())
		wins[0] = wm.focused->win;
	else if (dock.state & BELOW)
		wins[0] = wm.layertop[LAYER_DESK];
	else
		wins[0] = wm.layertop[LAYER_DOCK];
	wins[1] = dock.obj.win;
	XRestackWindows(dpy, wins, 2);
}

static void
dockdecorate(void)
{
	Bool isfullscreen;

	updatepixmap(&dock.pix, &dock.pw, &dock.ph, dock.w, dock.h);
	isfullscreen = (config.dockgravity[0] != '\0' && config.dockgravity[1] == 'F');
	switch (config.dockgravity[0]) {
	case 'N':
		drawbackground(
			dock.pix,
			(isfullscreen ? -config.shadowthickness : 0),
			-config.shadowthickness,
			dock.w + (isfullscreen ? 2 * config.shadowthickness : 0),
			dock.h + config.shadowthickness, STYLE_OTHER
		);
		break;
	case 'S':
		drawbackground(
			dock.pix,
			(isfullscreen ? -config.shadowthickness : 0),
			0,
			dock.w + (isfullscreen ? 2 * config.shadowthickness : 0),
			dock.h + config.shadowthickness, STYLE_OTHER
		);
		break;
	case 'W':
		drawbackground(
			dock.pix,
			-config.shadowthickness,
			(isfullscreen ? -config.shadowthickness : 0),
			dock.w + config.shadowthickness,
			dock.h + (isfullscreen ? 2 * config.shadowthickness : 0),
			STYLE_OTHER
		);
		break;
	default:
	case 'E':
		drawbackground(
			dock.pix,
			0,
			(isfullscreen ? -config.shadowthickness : 0),
			dock.w + config.shadowthickness,
			dock.h + (isfullscreen ? 2 * config.shadowthickness : 0),
			STYLE_OTHER
		);
		break;
	}
	drawcommit(dock.pix, dock.obj.win);
}

static void
handle_configure(struct Object *self, unsigned int valuemask, XWindowChanges *wc)
{
	struct Dockapp *dapp = self->self;

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
	handle_configure(
		&dapp->obj,
		CWWidth | CWHeight,
		&(XWindowChanges){
			.width = dapp->w,
			.height = dapp->h,
		}
	);
}

/* compute dockapp position given its width or height */
static int
dockapppos(int pos)
{
	return max(0, config.dockwidth / 2 - pos / 2);
}

static void
dappmoveresize(struct Dockapp *dapp)
{
	XMoveResizeWindow(
		dpy, dapp->obj.win,
		dapp->x, dapp->y,
		dapp->w, dapp->h
	);
	window_configure_notify(
		dpy, dapp->obj.win,
		dock.x + dapp->x, dock.y + dapp->y,
		dapp->w, dapp->h
	);
}

/* update dock position; create it, if necessary */
static void
dockupdateresizeable(void)
{
	struct Monitor *mon = wm.monitors[0];
	struct Object *p;
	struct Dockapp *dapp;
	int size;

	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = p->self;
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
		dock.x = mon->geometry.x;
		dock.y = mon->geometry.y;
		break;
	case 'S':
		dock.h = config.dockwidth;
		dock.x = mon->geometry.x;
		dock.y = mon->geometry.y + mon->geometry.height - config.dockwidth;
		break;
	case 'W':
		dock.w = config.dockwidth;
		dock.x = mon->geometry.x;
		dock.y = mon->geometry.y;
		break;
	case 'E':
	default:
		dock.w = config.dockwidth;
		dock.x = mon->geometry.x + mon->geometry.width - config.dockwidth;
		dock.h = min(size, mon->geometry.height);
		dock.y = mon->geometry.y + mon->geometry.height / 2 - size / 2;
		break;
	}
	if (config.dockgravity[0] == 'N' || config.dockgravity[0] == 'S') {
		switch (config.dockgravity[1]) {
		case 'F':
			dock.x = mon->geometry.x;
			dock.w = mon->geometry.width;
			break;
		case 'W':
			dock.w = min(size, mon->geometry.width);
			dock.x = mon->geometry.x;
			break;
		case 'E':
			dock.w = min(size, mon->geometry.width);
			dock.x = mon->geometry.x + mon->geometry.width - size;
			break;
		default:
			dock.w = min(size, mon->geometry.width);
			dock.x = mon->geometry.x + mon->geometry.width / 2 - size / 2;
			break;
		}
	} else if (config.dockgravity[0] != '\0') {
		switch (config.dockgravity[1]) {
		case 'F':
			dock.h = mon->geometry.height;
			dock.y = mon->geometry.y;
			break;
		case 'N':
			dock.h = min(size, mon->geometry.height);
			dock.y = mon->geometry.y;
			break;
		case 'S':
			dock.h = min(size, mon->geometry.height);
			dock.y = mon->geometry.y + mon->geometry.height - size;
			break;
		default:
			dock.h = min(size, mon->geometry.height);
			dock.y = mon->geometry.y + mon->geometry.height / 2 - size / 2;
			break;
		}
	}
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = p->self;
		dappmoveresize(dapp);
	}
}

/* update dock position; create it, if necessary */
static void
dockupdatefull(void)
{
	struct Object *p;
	struct Monitor *mon = wm.monitors[0];
	struct Dockapp *dapp;
	int part, nextend, size;
	int i, n;

	switch (config.dockgravity[0]) {
	case 'N':
		dock.x = mon->geometry.x;
		dock.y = mon->geometry.y;
		dock.w = mon->geometry.width;
		dock.h = config.dockwidth;
		part = dock.w;
		break;
	case 'S':
		dock.x = mon->geometry.x;
		dock.y = mon->geometry.y + mon->geometry.height - config.dockwidth;
		dock.w = mon->geometry.width;
		dock.h = config.dockwidth;
		part = dock.w;
		break;
	case 'W':
		dock.x = mon->geometry.x;
		dock.y = mon->geometry.y;
		dock.w = config.dockwidth;
		dock.h = mon->geometry.height;
		part = dock.h;
		break;
	case 'E':
	default:
		dock.x = mon->geometry.x + mon->geometry.width - config.dockwidth;
		dock.y = mon->geometry.y;
		dock.w = config.dockwidth;
		dock.h = mon->geometry.height;
		part = dock.h;
		break;
	}
	nextend = 0;
	size = 0;
	TAILQ_FOREACH(p, &dock.dappq, entry) {
		dapp = p->self;
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
		dapp = p->self;
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
		dappmoveresize(dapp);
		size += n;
	}
}

static void
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
	restack();
	if (dock.state & MINIMIZED)
		XUnmapWindow(dpy, dock.obj.win);
	else
		XMapWindow(dpy, dock.obj.win);
	XMapSubwindows(dpy, dock.obj.win);
done:
	shoddocks();
}

static void
update_window_area(void)
{
	struct Monitor *mon = wm.monitors[0];
	int left, right, top, bottom;

	dockupdate();
	left = right = top = bottom = 0;
	if (!TAILQ_EMPTY(&dock.dappq) &&
	    (dock.state & MAXIMIZED) && !(dock.state & MINIMIZED)) {
		switch (config.dockgravity[0]) {
		case 'N':
			top = config.dockwidth;
			break;
		case 'S':
			bottom = config.dockwidth;
			break;
		case 'W':
			left = config.dockwidth;
			break;
		case 'E':
		default:
			right = config.dockwidth;
			break;
		}
	}
	mon->window_area = (XRectangle){
		.x = max(mon->window_area.x, mon->geometry.x + left),
		.y = max(mon->window_area.y, mon->geometry.y + top),
		.width = min(
			mon->window_area.width,
			mon->geometry.width - left - right
		),
		.height = min(
			mon->window_area.height,
			mon->geometry.height - top - bottom
		),
	};
	mon->window_area.width = max(mon->window_area.width, 1);
	mon->window_area.height = max(mon->window_area.height, 1);
}

static void
manage(struct Object *p, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Dockapp *dapp;

	(void)p;
	(void)mon;
	(void)desk;
	(void)leader;
	dapp = emalloc(sizeof(*dapp));
	*dapp = (struct Dockapp){
		.obj.win = win,
		.obj.self = dapp,
		.obj.class = &dockapp_class,
		.x = 0,
		.y = 0,
		.w = rect.width,
		.h = rect.height,
		.dockpos = rect.x,
		.state = state,
	};
	context_add(win, &dapp->obj);
	dockappinsert(dapp);
	XReparentWindow(dpy, win, dock.obj.win, 0, 0);
	dockupdate();
	update_window_area();
}

static void
unmanage(struct Object *obj)
{
	struct Dockapp *dapp = obj->self;

	TAILQ_REMOVE(&dock.dappq, (struct Object *)dapp, entry);
	XReparentWindow(dpy, dapp->obj.win, root, 0, 0);
	context_del(dapp->obj.win);
	free(dapp);
	dockupdate();
	update_window_area();
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
	update_window_area();
}

void
settitle(Window win, const char *title)
{
	struct {
		Atom prop, type;
	} props[] = {
		{ atoms[_NET_WM_NAME],          atoms[UTF8_STRING] },
		{ atoms[_NET_WM_ICON_NAME],     atoms[UTF8_STRING] },
		{ XA_WM_NAME,                   XA_STRING },
		{ XA_WM_ICON_NAME,              XA_STRING },
	};
	size_t len, i;

	len = strlen(title);
	for (i = 0; i < LEN(props); i++) {
		XChangeProperty(
			dpy, win,
			props[i].prop,
			props[i].type,
			8,
			PropModeReplace,
			(unsigned char *)title,
			len
		);
	}
}

static void
init(void)
{
	XSetWindowAttributes swa;

	TAILQ_INIT(&dock.dappq);
	dock.pix = None;
	swa.event_mask = SubstructureNotifyMask | SubstructureRedirectMask;
	swa.background_pixel = BlackPixel(dpy, screen);
	swa.border_pixel = BlackPixel(dpy, screen);
	swa.colormap = colormap;
	dock.obj.win = createwindow(root, (XRectangle){0,0,1,1}, 0, NULL);
	dock.state = MAXIMIZED;
	dock.obj.class = &dock_class;
	settitle(dock.obj.win, "shod's dock");
	context_add(dock.obj.win, &dock.obj);
}

static void
clean(void)
{
	struct Object *obj;

	while ((obj = TAILQ_FIRST(&dock.dappq)) != NULL)
		unmanage(obj);
	if (dock.pix != None)
		XFreePixmap(dpy, dock.pix);
	XDestroyWindow(dpy, dock.obj.win);
}

struct Class dock_class = {
	.setstate       = changestate,
	.manage         = NULL,
	.unmanage       = NULL,
	.monitor_reset  = dockupdate,
	.restack        = restack,
};

struct Class dockapp_class = {
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
	.init           = init,
	.clean          = clean,
	.handle_configure = handle_configure,
	.monitor_reset  = NULL,
};
