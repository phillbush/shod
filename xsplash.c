#include "shod.h"

/* create new splash screen */
static struct Splash *
splashnew(Window win, int w, int h)
{
	struct Splash *splash;

	splash = emalloc(sizeof(*splash));
	*splash = (struct Splash){
		.obj.win = win,
		.obj.type = TYPE_SPLASH,
		.w = w,
		.h = h,
	};
	((struct Object *)splash)->type = TYPE_SPLASH;
	XReparentWindow(dpy, win, root, 0, 0);
	TAILQ_INSERT_HEAD(&wm.splashq, (struct Object *)splash, entry);
	return splash;
}

/* center splash screen on monitor and raise it above other windows */
void
splashplace(struct Splash *splash)
{
	Window wins[2];
	fitmonitor(wm.selmon, &splash->x, &splash->y, &splash->w, &splash->h, 0.5);
	splash->x = wm.selmon->wx + (wm.selmon->ww - splash->w) / 2;
	splash->y = wm.selmon->wy + (wm.selmon->wh - splash->h) / 2;
	wins[1] = splash->obj.win;
	wins[0] = wm.layers[LAYER_NORMAL].frame;
	XMoveWindow(dpy, splash->obj.win, splash->x, splash->y);
	XRestackWindows(dpy, wins, 2);
}

/* delete splash screen window */
int
unmanagesplash(struct Object *obj, int dummy)
{
	struct Splash *splash;

	splash = (struct Splash *)obj;
	(void)dummy;
	TAILQ_REMOVE(&wm.splashq, (struct Object *)splash, entry);
	icccmdeletestate(splash->obj.win);
	free(splash);
	return 0;
}

/* add splash screen and center it on the screen */
void
managesplash(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int state, int ignoreunmap)
{
	struct Splash *splash;

	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)state;
	(void)ignoreunmap;
	splash = splashnew(win, rect.width, rect.height);
	icccmwmstate(splash->obj.win, NormalState);
	splashplace(splash);
	XMapWindow(dpy, splash->obj.win);
}
