#include "shod.h"

struct Splash {
	struct Object obj;
	struct Monitor *mon;
	int desk;
	XRectangle geometry;
};

static struct Queue managed_splashs;

/* center splash screen on monitor and raise it above other windows */
static void
splashplace(struct Monitor *mon, struct Splash *splash)
{
	fitmonitor(mon, &splash->geometry, 0.5);
	splash->geometry.x = mon->window_area.x
	                   + (mon->window_area.width - splash->geometry.width) / 2;
	splash->geometry.y = mon->window_area.y
	                   + (mon->window_area.height - splash->geometry.height) / 2;
	XMoveWindow(
		dpy, splash->obj.win,
		splash->geometry.x, splash->geometry.y
	);
}

/* (un)hide splash screen */
static void
splashhide(struct Splash *splash, int hide)
{
	if (hide)
		XUnmapWindow(dpy, splash->obj.win);
	else
		XMapWindow(dpy, splash->obj.win);
}

static void
splashrise(struct Splash *splash)
{
	Window wins[2];

	wins[1] = splash->obj.win;
	wins[0] = wm.layertop[LAYER_NORMAL];
	XRestackWindows(dpy, wins, 2);
}

static void
manage(struct Object *tab, struct Monitor *mon, int desk, Window win,
       Window leader, XRectangle rect, enum State state)
{
	struct Splash *splash;

	(void)tab;
	(void)leader;
	(void)state;
	splash = emalloc(sizeof(*splash));
	*splash = (struct Splash){
		.obj.win = win,
		.obj.self = splash,
		.obj.class = &splash_class,
		.geometry = rect,
	};
	context_add(win, &splash->obj);
	XMapWindow(dpy, win);
	TAILQ_INSERT_HEAD(&managed_splashs, (struct Object *)splash, entry);
	splash->mon = mon;
	splash->desk = desk;
	splashplace(mon, splash);
	splashrise(splash);
	splashhide(splash, REMOVE);
}

static void
unmanage(struct Object *obj)
{
	struct Splash *splash = obj->self;

	context_del(obj->win);
	TAILQ_REMOVE(&managed_splashs, (struct Object *)splash, entry);
	free(splash);
}

static void
init(void)
{
	TAILQ_INIT(&managed_splashs);
}

static void
clean(void)
{
	struct Object *obj;

	while ((obj = TAILQ_FIRST(&managed_splashs)) != NULL)
		unmanage(obj);
}

static void
btnpress(struct Object *obj, XButtonPressedEvent *press)
{
	struct Splash *splash = obj->self;

	if (press->button != Button1)
		return;
	splashrise(splash);
}

static void
monitor_delete(struct Monitor *mon)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry) {
		struct Splash *splash = obj->self;
		if (splash->mon == mon)
			splash->mon = NULL;
	}
}

static void
monitor_reset(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry) {
		struct Splash *splash = obj->self;
		if (splash->mon == NULL)
			splashplace(wm.selmon, splash);
	}
}

static void
show_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry)
		splashhide(obj->self, True);
}

static void
hide_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry)
		splashhide(obj->self, True);
}

static void
change_desktop(struct Monitor *mon, int desk_old, int desk_new)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry) {
		struct Splash *splash = obj->self;

		if (splash->mon != mon)
			continue;
		if (splash->desk == desk_new) {
			splashhide(splash, REMOVE);
		} else if (splash->desk == desk_old) {
			splashhide(splash, ADD);
		}
	}
}

struct Class splash_class = {
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
	.init           = init,
	.clean          = clean,
	.btnpress       = btnpress,
	.monitor_delete = monitor_delete,
	.monitor_reset  = monitor_reset,
	.show_desktop   = show_desktop,
	.hide_desktop   = hide_desktop,
	.change_desktop = change_desktop,
};
