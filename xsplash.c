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
		unmapwin(splash->frame);
	else
		mapwin(splash->frame);
}

void
splashrise(struct Splash *splash)
{
	Window wins[2];

	wins[1] = splash->frame;
	wins[0] = wm.layers[LAYER_NORMAL].frame;
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
	splash = splashnew(win, rect.width, rect.height);
	splash->mon = mon;
	splash->desk = desk;
	splashplace(mon, splash);
	splashrise(splash);
	splashhide(splash, REMOVE);
}

static void
unmanage(struct Object *obj)
{
	struct Splash *splash;

	splash = (struct Splash *)obj;
	TAILQ_REMOVE(&wm.splashq, (struct Object *)splash, entry);
	icccmdeletestate(splash->obj.win);
	XDestroyWindow(dpy, splash->frame);
	free(splash);
}

struct Class *splash_class = &(struct Class){
	.type           = TYPE_SPLASH,
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
};
