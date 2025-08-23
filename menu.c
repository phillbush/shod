#include <err.h>
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
	Window edge, corner_left, corner_right;
	Pixmap pixmap;
	int pw, ph;

	XRectangle frame_geometry;
	int ignoreunmap;                        /* number of unmapnotifys to ignore */
	char *name;                             /* client name */
};

static struct Queue managed_menus;

static struct {
	Pixmap edge;
	Pixmap corner_left;
	Pixmap corner_right;
} decorations;

static void
redecorate(struct Object *obj)
{
	struct Menu *menu = obj->self;

	updatepixmap(
		&menu->pixmap, &menu->pw, &menu->ph,
		menu->frame_geometry.width,
		menu->frame_geometry.height
	);
	drawshadow(
		menu->pixmap, 0, 0,
		menu->frame_geometry.width,
		menu->frame_geometry.height,
		UNFOCUSED
	);
	drawshadow(
		menu->pixmap, 0, 0,
		menu->frame_geometry.width,
		config.titlewidth, FOCUSED
	);
	drawtitle(
		menu->pixmap, menu->name,
		menu->frame_geometry.width,
		wm.focused == &menu->obj,
		FOCUSED, False,
		wm.focused == &menu->obj
	);
	drawcommit(menu->pixmap, menu->obj.frame);
	drawcommit(menu->pixmap, menu->titlebar);
}

static void
menumoveresize(struct Menu *menu)
{
	XMoveResizeWindow(
		wm.display, menu->obj.frame,
		menu->frame_geometry.x, menu->frame_geometry.y,
		menu->frame_geometry.width, menu->frame_geometry.height
	);
	XResizeWindow(
		wm.display, menu->titlebar,
		menu->frame_geometry.width, config.titlewidth
	);
	XResizeWindow(
		wm.display, menu->obj.win,
		max(1, menu->frame_geometry.width - 2 * BORDER),
		max(1, menu->frame_geometry.height - BORDER - config.titlewidth - config.borderwidth)
	);
	XResizeWindow(
		wm.display, menu->edge,
		menu->frame_geometry.width, config.borderwidth
	);
	menu->mon = getmon(menu->frame_geometry.x, menu->frame_geometry.y);
	redecorate(&menu->obj);
	window_configure_notify(
		wm.display, menu->obj.win,
		menu->frame_geometry.x + BORDER,
		menu->frame_geometry.y + config.titlewidth,
		menu->frame_geometry.width - 2 * BORDER,
		menu->frame_geometry.height - BORDER - config.titlewidth - config.borderwidth
	);
}

static void
menufocus(struct Menu *menu)
{
	struct Object *prevfocused = wm.focused;

	if (wm.focused == &menu->obj)
		return;
	XSetInputFocus(wm.display, menu->obj.win, RevertToPointerRoot, CurrentTime);
	wm.focused = &menu->obj;
	if (prevfocused != NULL && prevfocused->self != menu) {
		if (prevfocused->class->redecorate != NULL)
			prevfocused->class->redecorate(prevfocused);
	}
	redecorate(&menu->obj);
}

/* put menu on beginning of menu list */
static void
menufocusraise(struct Menu *menu)
{
	TAILQ_REMOVE(&wm.stacking_order, &menu->obj, z_entry);
	TAILQ_INSERT_AFTER(
		&wm.stacking_order,
		&wm.layers[LAYER_MENU], &menu->obj,
		z_entry
	);
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
	XRestackWindows(wm.display, (Window[]){
		[0] = wm.layers[LAYER_MENU].frame,
		[1] = menu->obj.frame,
	}, 2);
}

static void
manage(struct Object *app, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Menu *menu;
	int cornerwidth = config.titlewidth + config.borderwidth;

	(void)mon;
	(void)desk;
	(void)state;

	/* adjust geometry for border on all sides and titlebar on top */
	rect.x -= BORDER;
	rect.y -= config.titlewidth;
	rect.width += 2 * BORDER;
	rect.height += BORDER + config.titlewidth + config.borderwidth;

	if (leader == None)
		leader = app->win;
	menu = emalloc(sizeof(*menu));
	*menu = (struct Menu){
		.titlebar = None,
		.obj.win = win,
		.obj.self = menu,
		.obj.class = &menu_class,
		.obj.frame = createframe(rect),
		.pixmap = None,
		.frame_geometry = rect,
	};
	menu->titlebar = createdecoration(
		menu->obj.frame,
		(XRectangle){0, 0, menu->frame_geometry.width, config.titlewidth},
		None, NorthWestGravity
	);
	menu->close_btn = createdecoration(
		menu->titlebar, (XRectangle){
			menu->frame_geometry.width - config.titlewidth, 0,
			config.titlewidth, config.titlewidth
		},
		wm.cursors[CURSOR_PIRATE], NorthEastGravity
	);
	menu->corner_left = createdecoration(
		menu->obj.frame,
		(XRectangle){
			0, menu->frame_geometry.height - config.borderwidth,
			cornerwidth + config.shadowthickness,
			config.borderwidth
		},
		wm.cursors[CURSOR_SW], SouthWestGravity
	);
	menu->corner_right = createdecoration(
		menu->obj.frame,
		(XRectangle){
			menu->frame_geometry.width - cornerwidth - config.shadowthickness,
			menu->frame_geometry.height - config.borderwidth,
			cornerwidth + config.shadowthickness,
			config.borderwidth
		},
		wm.cursors[CURSOR_SE], SouthEastGravity
	);
	menu->edge = createdecoration(
		menu->obj.frame,
		(XRectangle){
			0, menu->frame_geometry.height - config.borderwidth,
			menu->frame_geometry.width, config.borderwidth
		},
		None, SouthGravity
	);
	drawcommit(wm.close_btn[FOCUSED][1], menu->close_btn);
	drawcommit(decorations.edge, menu->edge);
	drawcommit(decorations.corner_left, menu->corner_left);
	drawcommit(decorations.corner_right, menu->corner_right);
	context_add(win, &menu->obj);
	context_add(menu->obj.frame, &menu->obj);
	context_add(menu->titlebar, &menu->obj);
	context_add(menu->close_btn, &menu->obj);
	context_add(menu->corner_left, &menu->obj);
	context_add(menu->corner_right, &menu->obj);
	XReparentWindow(
		wm.display,
		menu->obj.win, menu->obj.frame,
		BORDER, config.titlewidth
	);
	XChangeProperty(
		wm.display, win, wm.atoms[_NET_FRAME_EXTENTS],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)(long[]){
			config.borderwidth,
			config.borderwidth,
			config.borderwidth + config.titlewidth,
			config.borderwidth,
		}, 4
	);
	XMapWindow(wm.display, menu->obj.win);
	XMapWindow(wm.display, menu->titlebar);
	XMapWindow(wm.display, menu->close_btn);
	XMapWindow(wm.display, menu->edge);
	XMapRaised(wm.display, menu->corner_left);
	XMapRaised(wm.display, menu->corner_right);

	menu->leader = leader;
	winupdatetitle(menu->obj.win, &menu->name);
	TAILQ_INSERT_HEAD(&managed_menus, (struct Object *)menu, entry);
	TAILQ_INSERT_AFTER(
		&wm.stacking_order,
		&wm.layers[LAYER_MENU], &menu->obj,
		z_entry
	);
	menuplace(mon, menu);           /* this will set menu->mon for us */
	menuraise(menu);
	if (menu->leader == None ||
	    (focused_follows_leader(menu->leader))) {
		XMapWindow(wm.display, menu->obj.frame);
		menufocus(menu);
	}
	redecorate(&menu->obj);
}

static void
unmanage(struct Object *obj)
{
	struct Menu *menu = obj->self;

	context_del(obj->win);
	TAILQ_REMOVE(&managed_menus, &menu->obj, entry);
	TAILQ_REMOVE(&wm.stacking_order, &menu->obj, z_entry);
	if (wm.focused == &menu->obj)
		wm.focused = NULL;
	if (menu->pixmap != None)
		XFreePixmap(wm.display, menu->pixmap);
	XReparentWindow(wm.display, menu->obj.win, wm.rootwin, 0, 0);
	XDestroyWindow(wm.display, menu->obj.frame);
	XDestroyWindow(wm.display, menu->titlebar);
	XDestroyWindow(wm.display, menu->close_btn);
	XDestroyWindow(wm.display, menu->edge);
	XDestroyWindow(wm.display, menu->corner_left);
	XDestroyWindow(wm.display, menu->corner_right);
	free(menu->name);
	free(menu);
}

static void
drag_move(struct Menu *menu, int xroot, int yroot)
{
	XEvent event;
	int x, y;

	if (XGrabPointer(
		wm.display, menu->obj.frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, wm.cursors[CURSOR_MOVE], CurrentTime
	) != GrabSuccess)
		return;
	for (;;) {
		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
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
	XUngrabPointer(wm.display, CurrentTime);
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
		wm.display, menu->obj.frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, cursor, CurrentTime
	) != GrabSuccess)
		return;
	for (;;) {
		int dx, dy;

		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
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
	XUngrabPointer(wm.display, CurrentTime);
}

static void
btnpress(struct Object *self, XButtonPressedEvent *press)
{
	struct Menu *menu = self->self;

	menufocusraise(menu);
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
	} else if (press->window == menu->corner_left && press->button == Button1) {
		drag_resize(menu, BOTTOM|LEFT, press->x_root, press->y_root);
	} else if (press->window == menu->corner_right && press->button == Button1) {
		drag_resize(menu, BOTTOM|RIGHT, press->x_root, press->y_root);
	} else if (press->window == menu->close_btn && press->button == Button1) {
		if (released_inside(wm.display, press))
			window_close(wm.display, menu->obj.win);
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
	XFreePixmap(wm.display, decorations.edge);
	XFreePixmap(wm.display, decorations.corner_left);
	XFreePixmap(wm.display, decorations.corner_right);
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
		redecorate(obj);
}

static void
handle_property(struct Object *self, Atom property)
{
	struct Menu *menu = self->self;

	if (property == XA_WM_NAME || property == wm.atoms[_NET_WM_NAME]) {
		winupdatetitle(menu->obj.win, &menu->name);
		redecorate(&menu->obj);
	}
}

static void
handle_message(struct Object *self, Atom message, long int data[5])
{
	struct Menu *menu = self->self;

	if (message == wm.atoms[_NET_WM_MOVERESIZE]) {
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
			XUngrabPointer(wm.display, CurrentTime);
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
	redecorate(&menu->obj);
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
		XMapWindow(wm.display, menu->obj.frame);
		XChangeProperty(
			wm.display, menu->obj.win, wm.atoms[WM_STATE], wm.atoms[WM_STATE],
			32, PropModeReplace, (void *)&(long[]){
				[0] = NormalState,
				[1] = None,
			}, 2
		);
	} else {
		XUnmapWindow(wm.display, menu->obj.frame);
		XChangeProperty(
			wm.display, menu->obj.win, wm.atoms[WM_STATE], wm.atoms[WM_STATE],
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
reload_theme(void)
{
	int cornerwidth = config.titlewidth + config.borderwidth;

	updatepixmap(
		&decorations.edge,
		NULL, NULL, 1, config.borderwidth
	);
	drawshadow(
		decorations.edge,
		-config.shadowthickness, 0,
		config.borderwidth, config.borderwidth, FOCUSED
	);

	/*
	 * In addition to the corner's shadows, the shadows of the edge
	 * are part of the corner.  That is a optimization hack so we do
	 * not need to redraw the borders whenever the menu is resized.
	 * The tip of the edge is then "anchored" to the corners.
	 */

	updatepixmap(
		&decorations.corner_left, NULL, NULL,
		cornerwidth + config.shadowthickness,
		config.borderwidth
	);
	drawshadow(
		decorations.corner_left,
		0, 0,
		cornerwidth, config.borderwidth, FOCUSED
	);
	drawshadow(
		decorations.corner_left,
		cornerwidth, 0,
		2*config.shadowthickness, config.borderwidth, FOCUSED
	);

	updatepixmap(
		&decorations.corner_right, NULL, NULL,
		cornerwidth + config.shadowthickness,
		config.borderwidth
	);
	drawshadow(
		decorations.corner_right,
		config.shadowthickness, 0,
		cornerwidth, config.borderwidth, FOCUSED
	);
	drawshadow(
		decorations.corner_right,
		-config.shadowthickness, 0,
		2*config.shadowthickness, config.borderwidth, FOCUSED
	);
}

static void
restack_all(void)
{
	Window wins[2];
	struct Object *obj;

	wins[0] = wm.layers[LAYER_MENU].frame;
	TAILQ_FOREACH(obj, &managed_menus, entry) {
		struct Menu *menu = (obj->self);
		update_visibility(menu);
		menu = obj->self;
		wins[1] = menu->obj.frame;
		XRestackWindows(wm.display, wins, 2);
		wins[0] = menu->obj.frame;
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
	.redecorate     = redecorate,
	.redecorate_all = redecorate_all,
	.handle_property = handle_property,
	.handle_configure = handle_configure,
	.handle_enter   = handle_enter,
	.handle_message = handle_message,
	.hide_desktop   = hide_desktop,
	.show_desktop   = hide_desktop,
	.reload_theme   = reload_theme,
	.restack_all    = restack_all,
};
