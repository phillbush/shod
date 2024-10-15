#include "shod.h"

#define BORDER          1       /* pixel size of decoration around menus */

static struct Menu *
menunew(Window win, int x, int y, int w, int h)
{
	struct Menu *menu;
	int framex, framey, framew, frameh, titlew;

	/* adjust geometry for border on all sides and titlebar on top */
	framex = x - BORDER;
	framey = y - config.titlewidth;
	framew = w + 2 * BORDER;
	frameh = h + BORDER + config.titlewidth;
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
	menu->frame = createframe((XRectangle){0, 0, framew, frameh});
	menu->titlebar = createdecoration(
		menu->frame,
		(XRectangle){0, 0, titlew, config.titlewidth},
		None, None
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
	return menu;
}

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
	menudecorate(menu, 0);
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

void
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
	menunotify(menu);
}

/* decorate menu */
void
menudecorate(struct Menu *menu, int titlepressed)
{
	int tw;

	tw = max(1, menu->w - config.titlewidth);
	updatepixmap(&menu->pixtitlebar, &menu->tw, NULL, tw, config.titlewidth);
	drawshadow(menu->pixtitlebar, 0, 0, menu->tw, config.titlewidth, FOCUSED, titlepressed);
	if (menu->name != NULL)
		drawtitle(menu->pixtitlebar, menu->name, menu->tw, 0, FOCUSED, 0, 1);
	drawcommit(menu->button, wm.decorations[FOCUSED].btn_right);
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

	(void)tab;
	(void)mon;
	(void)desk;
	(void)state;
	menu = menunew(win, rect.x, rect.y, rect.width, rect.height);
	menu->leader = leader;
	winupdatetitle(menu->obj.win, &menu->name);
	TAILQ_INSERT_HEAD(&wm.menuq, (struct Object *)menu, entry);
	icccmwmstate(menu->obj.win, NormalState);
	menuplace(mon, menu);           /* this will set menu->mon for us */
	menudecorate(menu, 0);
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
	struct Menu *menu;

	menu = (struct Menu *)obj;
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

struct Class *menu_class = &(struct Class){
	.type           = TYPE_MENU,
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
};
