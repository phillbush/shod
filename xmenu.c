#include "shod.h"

#define BORDER          1       /* pixel size of decoration around menus */

/* remove menu from the menu list */
static void
menudelraise(struct Menu *menu)
{
	if (TAILQ_EMPTY(&wm.menuq))
		return;
	TAILQ_REMOVE(&wm.menuq, (struct Object *)menu, entry);
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
	TAILQ_INSERT_HEAD(&wm.menuq, (struct Object *)menu, entry);
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
	wins[0] = wm.layers[LAYER_MENU].frame;
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
		.obj.class = menu_class,
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
	TAILQ_INSERT_HEAD(&wm.menuq, (struct Object *)menu, entry);
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

	TAILQ_FOREACH(obj, &wm.menuq, entry) {
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
btnpress(struct Object *obj, XButtonPressedEvent *ev)
{
	struct Menu *menu = (struct Menu *)obj;

	menufocusraise(menu);
#warning TODO: implement menu button presses
#warning TODO: implement resizing menu by dragging frame with MOD+Button3
	if (ev->window == menu->titlebar && ev->button == Button1) {
		drag_move(menu, ev->x_root, ev->y_root);
	} else if (isvalidstate(ev->state) && ev->button == Button1) {
		drag_move(menu, ev->x_root, ev->y_root);
	}
}

struct Class *menu_class = &(struct Class){
	.type           = TYPE_MENU,
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
	.btnpress       = btnpress,
};
