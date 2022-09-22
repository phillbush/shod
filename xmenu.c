#include "shod.h"

/* create new menu */
static struct Menu *
menunew(Window win, int x, int y, int w, int h, int ignoreunmap)
{
	struct Menu *menu;

	menu = emalloc(sizeof(*menu));
	*menu = (struct Menu){
		.titlebar = None,
		.button = None,
		.obj.win = win,
		.obj.type = TYPE_MENU,
		.pix = None,
		.pixbutton = None,
		.pixtitlebar = None,
		.x = x - config.borderwidth,
		.y = y - config.borderwidth,
		.w = w + config.borderwidth * 2,
		.h = h + config.borderwidth * 2 + config.titlewidth,
		.ignoreunmap = ignoreunmap,
	};
	menu->frame = XCreateWindow(dpy, root, 0, 0,
	                            w + config.borderwidth * 2,
	                            h + config.borderwidth * 2 + config.titlewidth, 0,
	                            depth, CopyFromParent, visual,
	                            clientmask, &clientswa),
	menu->titlebar = XCreateWindow(dpy, menu->frame, config.borderwidth, config.borderwidth,
	                               max(1, menu->w - 2 * config.borderwidth - config.titlewidth),
	                               config.titlewidth, 0,
	                               depth, CopyFromParent, visual,
	                               clientmask, &clientswa);
	menu->button = XCreateWindow(dpy, menu->frame, menu->w - config.borderwidth - config.titlewidth, config.borderwidth,
	                             config.titlewidth, config.titlewidth, 0,
	                             depth, CopyFromParent, visual,
	                             clientmask, &clientswa);
	menu->pixbutton = XCreatePixmap(dpy, menu->button, config.titlewidth, config.titlewidth, depth);
	XDefineCursor(dpy, menu->button, wm.cursors[CURSOR_PIRATE]);
	XReparentWindow(dpy, menu->obj.win, menu->frame, config.borderwidth, config.borderwidth + config.titlewidth);
	XMapWindow(dpy, menu->obj.win);
	XMapWindow(dpy, menu->button);
	XMapWindow(dpy, menu->titlebar);
	return menu;
}

/* remove menu from the menu list */
static void
menudelraise(struct Menu *menu)
{
	if (TAILQ_EMPTY(menu->menuq))
		return;
	TAILQ_REMOVE(menu->menuq, (struct Object *)menu, entry);
}

/* configure menu window */
void
menuconfigure(struct Menu *menu, unsigned int valuemask, XWindowChanges *wc)
{
	if (menu == NULL)
		return;
	if (valuemask & CWX)
		menu->x = wc->x;
	if (valuemask & CWY)
		menu->y = wc->y;
	if (valuemask & CWWidth)
		menu->w = wc->width;
	if (valuemask & CWHeight)
		menu->h = wc->height;
	menumoveresize(menu);
	menudecorate(menu, 0);
}

void
menuincrmove(struct Menu *menu, int x, int y)
{
	menu->x += x;
	menu->y += y;
	//snaptoedge(&menu->x, &menu->y, menu->w, menu->h);
	XMoveWindow(dpy, menu->frame, menu->x, menu->y);
}

/* commit menu geometry */
void
menumoveresize(struct Menu *menu)
{
	struct Monitor *mon;

	XMoveResizeWindow(dpy, menu->frame, menu->x, menu->y, menu->w, menu->h);
	XMoveWindow(dpy, menu->button, menu->w - config.borderwidth - config.titlewidth, config.borderwidth);
	XResizeWindow(dpy, menu->titlebar, max(1, menu->w - 2 * config.borderwidth - config.titlewidth), config.titlewidth);
	XResizeWindow(dpy, menu->obj.win, menu->w - 2 * config.borderwidth, menu->h - 2 * config.borderwidth - config.titlewidth);
	if (menu->tab == NULL && (mon = getmon(menu->x, menu->y)) != NULL) {
		menu->mon = mon;
	}
}

/* decorate menu */
void
menudecorate(struct Menu *menu, int titlepressed)
{
	int tw, th;

	if (menu->pw != menu->w || menu->ph != menu->h || menu->pix == None)
		pixmapnew(&menu->pix, menu->frame, menu->w, menu->h);
	menu->pw = menu->w;
	menu->ph = menu->h;
	tw = max(1, menu->w - 2 * config.borderwidth - config.titlewidth);
	th = config.titlewidth;
	if (menu->tw != tw || menu->th != th || menu->pixtitlebar == None)
		pixmapnew(&menu->pixtitlebar, menu->titlebar, tw, th);
	menu->tw = tw;
	menu->th = th;

	drawbackground(menu->pix, 0, 0, menu->w, menu->h, FOCUSED);
	drawborders(menu->pix, menu->w, menu->h, FOCUSED);

	drawbackground(menu->pixtitlebar, 0, 0, menu->tw, menu->th, FOCUSED);
	drawshadow(menu->pixtitlebar, 0, 0, menu->tw, config.titlewidth, FOCUSED, titlepressed);
	/* write menu title */
	if (menu->name != NULL)
		drawtitle(menu->pixtitlebar, menu->name, menu->tw, 0, FOCUSED, 0, 1);
	buttonrightdecorate(menu->button, menu->pixbutton, FOCUSED, 0);
	drawcommit(menu->pix, menu->frame, menu->pw, menu->ph);
	drawcommit(menu->pixtitlebar, menu->titlebar, menu->tw, menu->th);
}

/* put menu on beginning of menu list */
void
menuaddraise(struct Menu *menu)
{
	menudelraise(menu);
	TAILQ_INSERT_HEAD(menu->menuq, (struct Object *)menu, entry);
	XSetInputFocus(dpy, menu->obj.win, RevertToParent, CurrentTime);
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

/* (un)hide menu */
void
menuhide(struct Menu *menu, int hide)
{
	if (hide)
		XUnmapWindow(dpy, menu->frame);
	else
		XMapWindow(dpy, menu->frame);
	icccmwmstate(menu->obj.win, (hide ? IconicState : NormalState));
}

/* assign menu to tab */
void
managemenu(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int state, int ignoreunmap)
{
	struct Queue *menuq;
	struct Menu *menu;

	(void)leader;
	(void)desk;
	(void)state;
	menu = menunew(win, rect.x, rect.y, rect.width, rect.height, ignoreunmap);
	if (tab) {
		menuq = &tab->menuq;
		mon = tab->row->col->c->mon;
	} else {
		menuq = &wm.menuq;
	}
	menu->mon = mon;
	menu->menuq = menuq;
	menu->tab = tab;
	winupdatetitle(menu->obj.win, &menu->name);
	TAILQ_INSERT_HEAD(menuq, (struct Object *)menu, entry);
	icccmwmstate(menu->obj.win, NormalState);
	menuplace(mon, menu);
	menudecorate(menu, 0);
	if (tab != NULL && wm.focused != NULL && wm.focused->selcol->selrow->seltab == tab) {
		tabfocus(tab, 0);
	} else {
		menuraise(menu);
		XMapWindow(dpy, menu->frame);
	}
}

/* delete menu; return whether menu was deleted */
int
unmanagemenu(struct Object *obj, int ignoreunmap)
{
	struct Menu *menu;

	menu = (struct Menu *)obj;
	if (ignoreunmap && menu->ignoreunmap) {
		menu->ignoreunmap--;
		return 0;
	}
	menudelraise(menu);
	if (menu->pix != None)
		XFreePixmap(dpy, menu->pix);
	if (menu->pixbutton != None)
		XFreePixmap(dpy, menu->pixbutton);
	if (menu->pixtitlebar != None)
		XFreePixmap(dpy, menu->pixtitlebar);
	icccmdeletestate(menu->obj.win);
	XReparentWindow(dpy, menu->obj.win, root, 0, 0);
	XDestroyWindow(dpy, menu->frame);
	XDestroyWindow(dpy, menu->titlebar);
	XDestroyWindow(dpy, menu->button);
	free(menu->name);
	free(menu);
	return 1;
}