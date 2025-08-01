#include "shod.h"

#define BORDER          1       /* pixel size of decoration around menus */

enum direction {
	TOP    = (1 << 0),
	BOTTOM = (1 << 1),
	LEFT   = (1 << 2),
	RIGHT  = (1 << 3),
};

struct Menu {
	struct Object obj;
	struct Monitor *mon;
	Window leader;

	Window titlebar;
	Window close_btn;
	Window frame;                           /* frame window */
	Pixmap pixtitlebar;                     /* pixmap to draw the titlebar */
	int pw, ph;                             /* pixmap size */
	int tw;                                 /* titlebar pixmap size */

	XRectangle frame_geometry;
	int ignoreunmap;                        /* number of unmapnotifys to ignore */
	char *name;                             /* client name */
};

static struct Queue managed_menus;

/* remove menu from the menu list */
static void
menudelraise(struct Menu *menu)
{
	if (TAILQ_EMPTY(&managed_menus))
		return;
	TAILQ_REMOVE(&managed_menus, (struct Object *)menu, entry);
}

static void
menudecorate(struct Menu *menu)
{
	updatepixmap(
		&menu->pixtitlebar, &menu->tw, NULL,
		menu->frame_geometry.width, config.titlewidth
	);
	drawshadow(
		menu->pixtitlebar, 0, 0,
		menu->frame_geometry.width,
		config.titlewidth, FOCUSED
	);
	drawtitle(
		menu->pixtitlebar, menu->name,
		menu->frame_geometry.width, 0,
		FOCUSED, 0, 1
	);
	drawcommit(menu->pixtitlebar, menu->titlebar);
}

/* commit menu geometry */
static void
menumoveresize(struct Menu *menu)
{
	XMoveResizeWindow(
		dpy, menu->frame,
		menu->frame_geometry.x, menu->frame_geometry.y,
		menu->frame_geometry.width, menu->frame_geometry.height
	);
	XResizeWindow(
		dpy, menu->titlebar,
		menu->frame_geometry.width, config.titlewidth
	);
	XResizeWindow(
		dpy, menu->obj.win,
		max(1, menu->frame_geometry.width - 2 * BORDER),
		max(1, menu->frame_geometry.height - BORDER - config.titlewidth)
	);
	menu->mon = getmon(menu->frame_geometry.x, menu->frame_geometry.y);
	window_configure_notify(
		dpy, menu->obj.win,
		menu->frame_geometry.x + BORDER,
		menu->frame_geometry.y + config.titlewidth,
		menu->frame_geometry.width - 2 * BORDER,
		menu->frame_geometry.height - BORDER - config.titlewidth
	);
}

static void
menufocus(struct Menu *menu)
{
	XSetInputFocus(dpy, menu->obj.win, RevertToPointerRoot, CurrentTime);
}

/* put menu on beginning of menu list */
static void
menufocusraise(struct Menu *menu)
{
	menudelraise(menu);
	TAILQ_INSERT_HEAD(&managed_menus, (struct Object *)menu, entry);
	menufocus(menu);
}

/* place menu next to its container */
static void
menuplace(struct Monitor *mon, struct Menu *menu)
{
	fitmonitor(mon, &menu->frame_geometry, 1.0);
	menumoveresize(menu);
}

/* raise desktop menu */
static void
menuraise(struct Menu *menu)
{
	Window wins[2];

	wins[1] = menu->frame;
	wins[0] = wm.layertop[LAYER_MENU];
	XRestackWindows(dpy, wins, 2);
}

static void
manage(struct Object *app, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Menu *menu;

	(void)mon;
	(void)desk;
	(void)state;

	/* adjust geometry for border on all sides and titlebar on top */
	rect.x -= BORDER;
	rect.y -= config.titlewidth;
	rect.width += 2 * BORDER;
	rect.height += BORDER + config.titlewidth;

	if (leader == None)
		leader = app->win;
	menu = emalloc(sizeof(*menu));
	*menu = (struct Menu){
		.titlebar = None,
		.obj.win = win,
		.obj.self = menu,
		.obj.class = &menu_class,
		.pixtitlebar = None,
		.frame_geometry = rect,
	};
	menu->frame = createframe(menu->frame_geometry);
	menu->titlebar = createdecoration(
		menu->frame,
		(XRectangle){0, 0, menu->frame_geometry.width, config.titlewidth},
		None, NorthWestGravity
	);
	menu->close_btn = createdecoration(
		menu->titlebar, (XRectangle){
			menu->frame_geometry.width - config.button_size - config.shadowthickness,
			config.shadowthickness,
			config.button_size, config.button_size
		},
		wm.cursors[CURSOR_PIRATE], NorthEastGravity
	);
	XGrabButton(
		dpy, AnyButton, AnyModifier,
		menu->frame, False, MOUSE_EVENTS,
		GrabModeSync, GrabModeAsync, None, None
	);
	drawcommit(wm.close_btn[FOCUSED][1], menu->close_btn);
	context_add(win, &menu->obj);
	context_add(menu->frame, &menu->obj);
	context_add(menu->titlebar, &menu->obj);
	context_add(menu->close_btn, &menu->obj);
	XReparentWindow(
		dpy,
		menu->obj.win, menu->frame,
		BORDER, config.titlewidth
	);
	XChangeProperty(
		dpy, win, atoms[_NET_FRAME_EXTENTS],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)(long[]){
			config.borderwidth,
			config.borderwidth,
			config.borderwidth + config.titlewidth,
			config.borderwidth,
		}, 4
	);
	XMapWindow(dpy, menu->obj.win);
	XMapWindow(dpy, menu->titlebar);
	XMapWindow(dpy, menu->close_btn);

	menu->leader = leader;
	winupdatetitle(menu->obj.win, &menu->name);
	TAILQ_INSERT_HEAD(&managed_menus, (struct Object *)menu, entry);
	menuplace(mon, menu);           /* this will set menu->mon for us */
	menudecorate(menu);
	menuraise(menu);
	if (menu->leader == None ||
	    (wm.focused != NULL && focused_follows_leader(menu->leader))) {
		XMapWindow(dpy, menu->frame);
		menufocus(menu);
	}
}

static void
unmanage(struct Object *obj)
{
	struct Menu *menu = obj->self;

	context_del(obj->win);
	menudelraise(menu);
	if (menu->pixtitlebar != None)
		XFreePixmap(dpy, menu->pixtitlebar);
	XReparentWindow(dpy, menu->obj.win, root, 0, 0);
	XDestroyWindow(dpy, menu->frame);
	XDestroyWindow(dpy, menu->titlebar);
	XDestroyWindow(dpy, menu->close_btn);
	free(menu->name);
	free(menu);
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
	for (;;) {
		XMaskEvent(dpy, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		x = event.xmotion.x_root - xroot;
		y = event.xmotion.y_root - yroot;
		menu->frame_geometry.x += x;
		menu->frame_geometry.y += y;
		menumoveresize(menu);
		xroot = event.xmotion.x_root;
		yroot = event.xmotion.y_root;
	}
	XUngrabPointer(dpy, CurrentTime);
}

static void
drag_resize(struct Menu *menu, int direction, int xroot, int yroot)
{
	Cursor cursor;
	XEvent event;
	XMotionEvent *motion = &event.xmotion;
	int x, y;

	switch (direction) {
	case TOP | LEFT:
		cursor = wm.cursors[CURSOR_NW];
		break;
	case TOP | RIGHT:
		cursor = wm.cursors[CURSOR_NE];
		break;
	case TOP:
		direction = TOP;
		cursor = wm.cursors[CURSOR_N];
		break;
	case BOTTOM | LEFT:
		direction = BOTTOM | LEFT;
		cursor = wm.cursors[CURSOR_SW];
		break;
	case BOTTOM | RIGHT:
		direction = BOTTOM | RIGHT;
		cursor = wm.cursors[CURSOR_SE];
		break;
	case BOTTOM:
		direction = BOTTOM;
		cursor = wm.cursors[CURSOR_S];
		break;
	case LEFT:
		direction = LEFT;
		cursor = wm.cursors[CURSOR_W];
		break;
	case RIGHT:
		direction = RIGHT;
		cursor = wm.cursors[CURSOR_E];
		break;
	}
	if (direction & LEFT)
		x = xroot - menu->frame_geometry.x;
	else if (direction & RIGHT)
		x = menu->frame_geometry.x + menu->frame_geometry.width - xroot;
	else
		x = 0;
	if (direction & TOP)
		y = yroot - menu->frame_geometry.y;
	else if (direction & BOTTOM)
		y = menu->frame_geometry.y + menu->frame_geometry.height - yroot;
	else
		y = 0;
	if (XGrabPointer(
		dpy, menu->frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, cursor, CurrentTime
	) != GrabSuccess)
		return;
	for (;;) {
		int dx, dy;

		XMaskEvent(dpy, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		if (x > menu->frame_geometry.width) x = 0;
		if (y > menu->frame_geometry.height) y = 0;
		if (direction & LEFT &&
		    ((motion->x_root < xroot && x > motion->x_root - menu->frame_geometry.x) ||
		     (motion->x_root > xroot && x < motion->x_root - menu->frame_geometry.x))) {
			dx = xroot - motion->x_root;
			if (menu->frame_geometry.width + dx < wm.minsize) continue;
			menu->frame_geometry.x -= dx;
			menu->frame_geometry.width += dx;
		} else if (direction & RIGHT &&
		    ((motion->x_root > xroot && x >
		    menu->frame_geometry.x + menu->frame_geometry.width - motion->x_root) ||
		     (motion->x_root < xroot && x <
		     menu->frame_geometry.x + menu->frame_geometry.width - motion->x_root))) {
			dx = motion->x_root - xroot;
			if (menu->frame_geometry.width + dx < wm.minsize) continue;
			menu->frame_geometry.width += dx;
		}
		if (direction & TOP &&
		    ((motion->y_root < yroot && y > motion->y_root -
		    menu->frame_geometry.y) ||
		     (motion->y_root > yroot && y < motion->y_root -
		     menu->frame_geometry.y))) {
			dy = yroot - motion->y_root;
			if (menu->frame_geometry.height + dy < wm.minsize) continue;
			menu->frame_geometry.y -= dy;
			menu->frame_geometry.height += dy;
		} else if (direction & BOTTOM &&
		    ((motion->y_root > yroot && menu->frame_geometry.y +
		    menu->frame_geometry.height - motion->y_root < y) ||
		     (motion->y_root < yroot && menu->frame_geometry.y +
		     menu->frame_geometry.height - motion->y_root > y))) {
			dy = motion->y_root - yroot;
			if (menu->frame_geometry.height + dy < wm.minsize) continue;
			menu->frame_geometry.height += dy;
		}
		xroot = motion->x_root;
		yroot = motion->y_root;
		menumoveresize(menu);
	}
	XUngrabPointer(dpy, CurrentTime);
}

static void
btnpress(struct Object *self, XButtonPressedEvent *press)
{
	struct Menu *menu = self->self;

	menufocusraise(menu);
#warning TODO: implement menu button presses
	if (press->window == menu->titlebar && press->button == Button1) {
		drag_move(menu, press->x_root, press->y_root);
	} else if (isvalidstate(press->state) && press->button == Button1) {
		drag_move(menu, press->x_root, press->y_root);
	} else if (isvalidstate(press->state) && press->button == Button3) {
		enum direction direction;

		if (press->x <= menu->frame_geometry.width/2 && press->y <= menu->frame_geometry.height/2)
			direction = TOP | LEFT;
		else if (press->x > menu->frame_geometry.width/2 && press->y <= menu->frame_geometry.height/2)
			direction = TOP | RIGHT;
		else if (press->x <= menu->frame_geometry.width/2 && press->y > menu->frame_geometry.height/2)
			direction = BOTTOM | LEFT;
		else
			direction = BOTTOM | RIGHT;
		drag_resize(
			menu, direction,
			press->x_root, press->y_root
		);
	} else if (press->window == menu->close_btn && press->button == Button1) {
		window_close(dpy, menu->obj.win);
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
		struct Menu *menu = obj->self;
		if (menu->mon == mon)
			menu->mon = NULL;
	}
}

static void
monitor_reset(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_menus, entry) {
		struct Menu *menu = obj->self;
		if (menu->mon == NULL)
			menuplace(wm.selmon, menu);
	}
}

static void
redecorate_all(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_menus, entry)
		menudecorate(obj->self);
}

static void
handle_property(struct Object *self, Atom property)
{
	struct Menu *menu = self->self;

	if (property == XA_WM_NAME || property == atoms[_NET_WM_NAME]) {
		winupdatetitle(menu->obj.win, &menu->name);
		menudecorate(menu);
	}
}

static void
handle_message(struct Object *self, Atom message, long int data[5])
{
	struct Menu *menu = self->self;

	if (message == atoms[_NET_WM_MOVERESIZE]) {
		/*
		 * Client-side decorated Gtk3 windows emit this signal when being
		 * dragged by their GtkHeaderBar
		 */
		switch (data[2]) {
		case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
			drag_resize(menu, TOP|LEFT, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOP:
			drag_resize(menu, TOP, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
			drag_resize(menu, TOP|RIGHT, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_RIGHT:
			drag_resize(menu, RIGHT, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
			drag_resize(menu, BOTTOM|RIGHT, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
			drag_resize(menu, BOTTOM, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
			drag_resize(menu, BOTTOM|LEFT, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_LEFT:
			drag_resize(menu, LEFT, data[0], data[1]);
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
handle_configure(struct Object *self, unsigned int valuemask, XWindowChanges *wc)
{
	struct Menu *menu = self->self;

	if (menu == NULL)
		return;
	if (valuemask & CWX)
		menu->frame_geometry.x = wc->x - BORDER;
	if (valuemask & CWY)
		menu->frame_geometry.y = wc->y - config.titlewidth;
	if (valuemask & CWWidth)
		menu->frame_geometry.width = wc->width + 2 * BORDER;
	if (valuemask & CWHeight)
		menu->frame_geometry.height = wc->height + BORDER + config.titlewidth;
	menumoveresize(menu);
	menudecorate(menu);
}

static void
handle_enter(struct Object *self)
{
	struct Menu *menu = self->self;

	if (config.sloppyfocus)
		menufocusraise(menu);
}

static void
update_visibility(struct Menu *menu)
{
	if (!wm.showingdesk &&
	    (menu->leader == None || focused_follows_leader(menu->leader))) {
		XMapWindow(dpy, menu->frame);
		XChangeProperty(
			dpy, menu->obj.win, atoms[WM_STATE], atoms[WM_STATE],
			32, PropModeReplace, (void *)&(long[]){
				[0] = NormalState,
				[1] = None,
			}, 2
		);
	} else {
		XUnmapWindow(dpy, menu->frame);
		XChangeProperty(
			dpy, menu->obj.win, atoms[WM_STATE], atoms[WM_STATE],
			32, PropModeReplace, (void *)&(long[]){
				[0] = IconicState,
				[1] = None,
			}, 2
		);
	}
}

static void
hide_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_menus, entry)
		update_visibility(obj->self);
}

static void
restack(void)
{
	Window wins[2];
	struct Object *obj;

	wins[0] = wm.layertop[LAYER_MENU];
	TAILQ_FOREACH(obj, &managed_menus, entry) {
		struct Menu *menu = (obj->self);
		update_visibility(menu);
		if (!focused_follows_leader(menu->leader))
			continue;
		menu = obj->self;
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
	.handle_configure = handle_configure,
	.handle_enter   = handle_enter,
	.handle_message = handle_message,
	.hide_desktop   = hide_desktop,
	.show_desktop   = hide_desktop,
	.restack        = restack,
};
