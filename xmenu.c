#include "shod.h"

#define BORDER          1       /* pixel size of decoration around menus */
static struct Queue managed_menus;

/* remove menu from the menu list */
static void
menudelraise(struct Menu *menu)
{
	if (TAILQ_EMPTY(&managed_menus))
		return;
	TAILQ_REMOVE(&managed_menus, (struct Object *)menu, entry);
}

/* configure menu window */
void
menuconfigure(struct Menu *menu, unsigned int valuemask, XWindowChanges *wc)
{
	if (menu == NULL)
		return;
	if (valuemask & CWX)
		menu->x = wc->x - BORDER;
	if (valuemask & CWY)
		menu->y = wc->y - config.titlewidth;
	if (valuemask & CWWidth)
		menu->w = wc->width + 2 * BORDER;
	if (valuemask & CWHeight)
		menu->h = wc->height + BORDER + config.titlewidth;
	menumoveresize(menu);
	menudecorate(menu);
}

static void
menunotify(struct Menu *menu)
{
	winnotify(
		menu->obj.win,
		menu->x + BORDER,
		menu->y + config.titlewidth,
		menu->w - 2 * BORDER,
		menu->h - BORDER - config.titlewidth
	);
}

static void
menuincrmove(struct Menu *menu, int x, int y)
{
	menu->x += x;
	menu->y += y;
	XMoveWindow(dpy, menu->frame, menu->x, menu->y);
	menunotify(menu);
}

/* commit menu geometry */
void
menumoveresize(struct Menu *menu)
{
	XMoveResizeWindow(dpy, menu->frame, menu->x, menu->y, menu->w, menu->h);
	XMoveWindow(dpy, menu->button, menu->w - config.titlewidth, 0);
	XResizeWindow(
		dpy, menu->titlebar,
		max(1, menu->w - config.titlewidth),
		config.titlewidth
	);
	XResizeWindow(
		dpy, menu->obj.win,
		max(1, menu->w - 2 * BORDER),
		max(1, menu->h - BORDER - config.titlewidth)
	);
	menu->mon = getmon(menu->x, menu->y);
	menudecorate(menu);
	menunotify(menu);
}

/* decorate menu */
void
menudecorate(struct Menu *menu)
{
	int tw;

	tw = max(1, menu->w - config.titlewidth);
	updatepixmap(&menu->pixtitlebar, &menu->tw, NULL, tw, config.titlewidth);
	drawshadow(
		menu->pixtitlebar,
		0, 0, tw, config.titlewidth,
		FOCUSED, False, config.shadowthickness
	);
	drawtitle(menu->pixtitlebar, menu->name, tw, 0, FOCUSED, 0, 1);
	drawcommit(wm.decorations[FOCUSED].btn_right, menu->button);
	drawcommit(menu->pixtitlebar, menu->titlebar);
}

void
menufocus(struct Menu *menu)
{
	XSetInputFocus(dpy, menu->obj.win, RevertToParent, CurrentTime);
}

/* put menu on beginning of menu list */
void
menufocusraise(struct Menu *menu)
{
	menudelraise(menu);
	TAILQ_INSERT_HEAD(&managed_menus, (struct Object *)menu, entry);
	menufocus(menu);
}

/* place menu next to its container */
void
menuplace(struct Monitor *mon, struct Menu *menu)
{
	fitmonitor(mon, &menu->x, &menu->y, &menu->w, &menu->h, 1.0);
	menumoveresize(menu);
}

/* raise desktop menu */
void
menuraise(struct Menu *menu)
{
	Window wins[2];

	wins[1] = menu->frame;
	wins[0] = wm.layers[LAYER_MENU].obj.win;
	XRestackWindows(dpy, wins, 2);
}

static void
manage(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Menu *menu;
	int framex, framey, framew, frameh, titlew;

	(void)tab;
	(void)mon;
	(void)desk;
	(void)state;

	/* adjust geometry for border on all sides and titlebar on top */
	framex = rect.x - BORDER;
	framey = rect.y - config.titlewidth;
	framew = rect.width + 2 * BORDER;
	frameh = rect.height + BORDER + config.titlewidth;
	titlew = framew - config.titlewidth;
	if (titlew < 0)
		titlew = framew;

	menu = emalloc(sizeof(*menu));
	*menu = (struct Menu){
		.titlebar = None,
		.button = None,
		.obj.win = win,
		.obj.class = &menu_class,
		.pixtitlebar = None,
		.x = framex,
		.y = framey,
		.w = framew,
		.h = frameh,
	};
	context_add(win, &menu->obj);
	menu->frame = createframe((XRectangle){0, 0, framew, frameh});
	menu->titlebar = createdecoration(
		menu->frame,
		(XRectangle){0, 0, titlew, config.titlewidth},
		None, NorthWestGravity
	);
	menu->button = createdecoration(
		menu->frame,
		(XRectangle){titlew, 0, config.titlewidth, config.titlewidth},
		wm.cursors[CURSOR_PIRATE], NorthEastGravity
	);
	XReparentWindow(
		dpy,
		menu->obj.win, menu->frame,
		BORDER, config.titlewidth
	);
	XMapWindow(dpy, menu->obj.win);
	XMapWindow(dpy, menu->button);
	XMapWindow(dpy, menu->titlebar);

	menu->leader = leader;
	winupdatetitle(menu->obj.win, &menu->name);
	TAILQ_INSERT_HEAD(&managed_menus, (struct Object *)menu, entry);
	icccmwmstate(menu->obj.win, NormalState);
	menuplace(mon, menu);           /* this will set menu->mon for us */
	menudecorate(menu);
	menuraise(menu);
	if (menu->leader == None ||
	    (wm.focused != NULL &&
	     istabformenu(wm.focused->selcol->selrow->seltab, menu))) {
		XMapWindow(dpy, menu->frame);
		menufocus(menu);
	}
}

static void
unmanage(struct Object *obj)
{
	struct Menu *menu = (struct Menu *)obj;

	context_del(obj->win);
	menudelraise(menu);
	if (menu->pixtitlebar != None)
		XFreePixmap(dpy, menu->pixtitlebar);
	icccmdeletestate(menu->obj.win);
	XReparentWindow(dpy, menu->obj.win, root, 0, 0);
	XDestroyWindow(dpy, menu->frame);
	XDestroyWindow(dpy, menu->titlebar);
	XDestroyWindow(dpy, menu->button);
	free(menu->name);
	free(menu);
}

/* check if given tab accepts given menu */
int
istabformenu(struct Tab *tab, struct Menu *menu)
{
	return (menu->leader == tab->obj.win || menu->leader == tab->leader);
}

/* map menus for current focused tab */
void
menuupdate(void)
{
	struct Object *obj;
	struct Menu *menu;

	TAILQ_FOREACH(obj, &managed_menus, entry) {
		menu = ((struct Menu *)obj);
		if (menu->leader == None)
			continue;
		if (!wm.showingdesk && wm.focused != NULL && istabformenu(wm.focused->selcol->selrow->seltab, menu)) {
			XMapWindow(dpy, menu->frame);
			icccmwmstate(obj->win, NormalState);
		} else {
			XUnmapWindow(dpy, ((struct Menu *)obj)->frame);
			icccmwmstate(obj->win, IconicState);
		}
	}
}

static void
drag_move(struct Menu *menu, int xroot, int yroot)
{
	XEvent event;
	int x, y;

	if (XGrabPointer(
		dpy, menu->frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, wm.cursors[CURSOR_MOVE], CurrentTime
	) != GrabSuccess)
		return;
	while (!XMaskEvent(dpy, ButtonReleaseMask|PointerMotionMask, &event)) {
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		if (!compress_motion(&event))
			continue;
		x = event.xmotion.x_root - xroot;
		y = event.xmotion.y_root - yroot;
		menuincrmove(menu, x, y);
		xroot = event.xmotion.x_root;
		yroot = event.xmotion.y_root;
	}
	XUngrabPointer(dpy, CurrentTime);
}

static void
drag_resize(struct Menu *menu, int border, int xroot, int yroot)
{
	enum {
		TOP    = (1 << 0),
		BOTTOM = (1 << 1),
		LEFT   = (1 << 2),
		RIGHT  = (1 << 3),
	};
	Cursor cursor;
	XEvent event;
	XMotionEvent *motion = &event.xmotion;
	int x, y;
	int direction;

	switch (border) {
	case BORDER_NW:
		direction = TOP | LEFT;
		cursor = wm.cursors[CURSOR_NW];
		break;
	case BORDER_NE:
		direction = TOP | RIGHT;
		cursor = wm.cursors[CURSOR_NE];
		break;
	case BORDER_N:
		direction = TOP;
		cursor = wm.cursors[CURSOR_N];
		break;
	case BORDER_SW:
		direction = BOTTOM | LEFT;
		cursor = wm.cursors[CURSOR_SW];
		break;
	case BORDER_SE:
		direction = BOTTOM | RIGHT;
		cursor = wm.cursors[CURSOR_SE];
		break;
	case BORDER_S:
		direction = BOTTOM;
		cursor = wm.cursors[CURSOR_S];
		break;
	case BORDER_W:
		direction = LEFT;
		cursor = wm.cursors[CURSOR_W];
		break;
	case BORDER_E:
		direction = RIGHT;
		cursor = wm.cursors[CURSOR_E];
		break;
	}
	if (direction & LEFT)
		x = xroot - menu->x;
	else if (direction & RIGHT)
		x = menu->x + menu->w - xroot;
	else
		x = 0;
	if (direction & TOP)
		y = yroot - menu->y;
	else if (direction & BOTTOM)
		y = menu->y + menu->h - yroot;
	else
		y = 0;
	if (XGrabPointer(
		dpy, menu->frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, cursor, CurrentTime
	) != GrabSuccess)
		return;
	while (!XMaskEvent(dpy, ButtonReleaseMask|PointerMotionMask, &event)) {
		int dx, dy;

		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		if (x > menu->w) x = 0;
		if (y > menu->h) y = 0;
		if (direction & LEFT &&
		    ((motion->x_root < xroot && x > motion->x_root - menu->x) ||
		     (motion->x_root > xroot && x < motion->x_root - menu->x))) {
			dx = xroot - motion->x_root;
			if (menu->w + dx < wm.minsize) continue;
			menu->x -= dx;
			menu->w += dx;
		} else if (direction & RIGHT &&
		    ((motion->x_root > xroot && x > menu->x + menu->w - motion->x_root) ||
		     (motion->x_root < xroot && x < menu->x + menu->w - motion->x_root))) {
			dx = motion->x_root - xroot;
			if (menu->w + dx < wm.minsize) continue;
			menu->w += dx;
		}
		if (direction & TOP &&
		    ((motion->y_root < yroot && y > motion->y_root - menu->y) ||
		     (motion->y_root > yroot && y < motion->y_root - menu->y))) {
			dy = yroot - motion->y_root;
			if (menu->h + dy < wm.minsize) continue;
			menu->y -= dy;
			menu->h += dy;
		} else if (direction & BOTTOM &&
		    ((motion->y_root > yroot && menu->y + menu->h - motion->y_root < y) ||
		     (motion->y_root < yroot && menu->y + menu->h - motion->y_root > y))) {
			dy = motion->y_root - yroot;
			if (menu->h + dy < wm.minsize) continue;
			menu->h += dy;
		}
		xroot = motion->x_root;
		yroot = motion->y_root;
		if (!compress_motion(&event))
			continue;
		menumoveresize(menu);
	}
	XUngrabPointer(dpy, CurrentTime);
}

static void
btnpress(struct Object *self, XButtonPressedEvent *press)
{
	struct Menu *menu = (struct Menu *)self;

	menufocusraise(menu);
#warning TODO: implement menu button presses
	if (press->window == menu->titlebar && press->button == Button1) {
		drag_move(menu, press->x_root, press->y_root);
	} else if (isvalidstate(press->state) && press->button == Button1) {
		drag_move(menu, press->x_root, press->y_root);
	} else if (isvalidstate(press->state) && press->button == Button3) {
		enum border border;

		if (press->x <= menu->w/2 && press->y <= menu->h/2)
			border = BORDER_NW;
		else if (press->x > menu->w/2 && press->y <= menu->h/2)
			border = BORDER_NE;
		else if (press->x <= menu->w/2 && press->y > menu->h/2)
			border = BORDER_SW;
		else
			border = BORDER_SE;
		drag_resize(
			menu, border,
			press->x_root, press->y_root
		);
	}
}

static void
init(void)
{
	TAILQ_INIT(&managed_menus);
}

static void
clean(void)
{
	struct Object *obj;

	while ((obj = TAILQ_FIRST(&managed_menus)) != NULL)
		unmanage(obj);
}

static void
monitor_delete(struct Monitor *mon)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_menus, entry) {
		struct Menu *menu = (struct Menu *)obj;
		if (menu->mon == mon)
			menu->mon = NULL;
	}
}

static void
monitor_reset(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_menus, entry) {
		struct Menu *menu = (struct Menu *)obj;
		if (menu->mon == NULL)
			menuplace(wm.selmon, menu);
	}
}

static void
redecorate_all(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_menus, entry)
		menudecorate((struct Menu *)obj);
}

static void
handle_property(struct Object *self, Atom property)
{
	struct Menu *menu = (struct Menu *)self;

	if (property == XA_WM_NAME || property == atoms[_NET_WM_NAME]) {
		winupdatetitle(menu->obj.win, &menu->name);
		menudecorate(menu);
	}
}

static void
handle_message(struct Object *self, Atom message, long int data[5])
{
	struct Menu *menu = (struct Menu *)self;

	if (message == atoms[_NET_WM_MOVERESIZE]) {
		/*
		 * Client-side decorated Gtk3 windows emit this signal when being
		 * dragged by their GtkHeaderBar
		 */
		switch (data[2]) {
		case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
			drag_resize(menu, BORDER_NW, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOP:
			drag_resize(menu, BORDER_N, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
			drag_resize(menu, BORDER_NE, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_RIGHT:
			drag_resize(menu, BORDER_E, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
			drag_resize(menu, BORDER_SE, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
			drag_resize(menu, BORDER_S, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
			drag_resize(menu, BORDER_SW, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_LEFT:
			drag_resize(menu, BORDER_W, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_MOVE:
			drag_move(menu, data[0], data[1]);
			break;
		default:
			XUngrabPointer(dpy, CurrentTime);
			break;
		}
	}
}

static void
restack(void)
{
	Window wins[2];
	struct Object *obj;

	wins[0] = wm.layers[LAYER_MENU].obj.win;
	TAILQ_FOREACH(obj, &managed_menus, entry) {
		struct Menu *menu = ((struct Menu *)obj);
		if (!istabformenu(wm.focused->selcol->selrow->seltab, menu))
			continue;
		menu = (struct Menu *)obj;
		wins[1] = menu->frame;
		XRestackWindows(dpy, wins, 2);
		wins[0] = menu->frame;
	}
}

struct Class menu_class = {
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
	.btnpress       = btnpress,
	.init           = init,
	.clean          = clean,
	.monitor_delete = monitor_delete,
	.monitor_reset  = monitor_reset,
	.redecorate_all = redecorate_all,
	.handle_property = handle_property,
	.restack        = restack,
};
