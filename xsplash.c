#include "shod.h"

/* create new splash screen */
static struct Splash *
splashnew(Window win, int w, int h)
{
	struct Splash *splash;

	splash = emalloc(sizeof(*splash));
	*splash = (struct Splash){
		.obj.win = win,
		.obj.class = splash_class,
		.w = w,
		.h = h,
	};
	splash->frame = XCreateWindow(
		dpy,
		root,
		0, 0,
		w, h,
		0,
		depth, CopyFromParent, visual,
		clientmask, &clientswa
	);
	XReparentWindow(dpy, win, splash->frame, 0, 0);
	XMapWindow(dpy, win);
	TAILQ_INSERT_HEAD(&wm.splashq, (struct Object *)splash, entry);
	return splash;
}

/* center splash screen on monitor and raise it above other windows */
void
splashplace(struct Monitor *mon, struct Splash *splash)
{
	fitmonitor(mon, &splash->x, &splash->y, &splash->w, &splash->h, 0.5);
	splash->x = mon->wx + (mon->ww - splash->w) / 2;
	splash->y = mon->wy + (mon->wh - splash->h) / 2;
	XMoveWindow(dpy, splash->frame, splash->x, splash->y);
}

/* (un)hide splash screen */
void
splashhide(struct Splash *splash, int hide)
{
	if (hide)
		XUnmapWindow(dpy, splash->frame);
	else
		XMapWindow(dpy, splash->frame);
	icccmwmstate(splash->frame, (hide ? IconicState : NormalState));
}

void
splashrise(struct Splash *splash)
{
	Window wins[2];

	wins[1] = splash->frame;
	wins[0] = wm.layers[LAYER_NORMAL].frame;
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
	XDestroyWindow(dpy, splash->frame);
	free(splash);
	return 0;
}

/* add splash screen and center it on the screen */
void
managesplash(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int state, int ignoreunmap)
{
	struct Splash *splash;

	(void)tab;
	(void)leader;
	(void)state;
	(void)ignoreunmap;
	splash = splashnew(win, rect.width, rect.height);
	splash->mon = mon;
	splash->desk = desk;
	splashplace(mon, splash);
	splashrise(splash);
	splashhide(splash, REMOVE);
}

Class *splash_class = &(Class){
	.type           = TYPE_SPLASH,
	.setstate       = NULL,
};
