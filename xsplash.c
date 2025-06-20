#include "shod.h"

/* center splash screen on monitor and raise it above other windows */
void
splashplace(struct Monitor *mon, struct Splash *splash)
{
	fitmonitor(mon, &splash->x, &splash->y, &splash->w, &splash->h, 0.5);
	splash->x = mon->wx + (mon->ww - splash->w) / 2;
	splash->y = mon->wy + (mon->wh - splash->h) / 2;
	XMoveWindow(dpy, splash->obj.win, splash->x, splash->y);
}

/* (un)hide splash screen */
void
splashhide(struct Splash *splash, int hide)
{
	if (hide)
		unmapwin(splash->obj.win);
	else
		mapwin(splash->obj.win);
}

void
splashrise(struct Splash *splash)
{
	Window wins[2];

	wins[1] = splash->obj.win;
	wins[0] = wm.layers[LAYER_NORMAL].obj.win;
	XRestackWindows(dpy, wins, 2);
}

static void
manage(struct Tab *tab, struct Monitor *mon, int desk, Window win,
       Window leader, XRectangle rect, enum State state)
{
	struct Splash *splash;

	(void)tab;
	(void)leader;
	(void)state;
	splash = emalloc(sizeof(*splash));
	*splash = (struct Splash){
		.obj.win = win,
		.obj.class = splash_class,
		.w = rect.width,
		.h = rect.height,
	};
	context_add(win, &splash->obj);
	XMapWindow(dpy, win);
	TAILQ_INSERT_HEAD(&wm.splashq, (struct Object *)splash, entry);
	splash->mon = mon;
	splash->desk = desk;
	splashplace(mon, splash);
	splashrise(splash);
	splashhide(splash, REMOVE);
}

static void
unmanage(struct Object *obj)
{
	struct Splash *splash = (struct Splash *)obj;

	context_del(obj->win);
	TAILQ_REMOVE(&wm.splashq, (struct Object *)splash, entry);
	icccmdeletestate(splash->obj.win);
	free(splash);
}

struct Class *splash_class = &(struct Class){
	.type           = TYPE_SPLASH,
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
};
