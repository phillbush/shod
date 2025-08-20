#include "shod.h"

struct Splash {
	struct Object obj;
	struct Monitor *mon;
	int desk;
	XRectangle geometry;
	Pixmap pixmap;
	int pixwidth, pixheight;
};

static struct Queue managed_splashs;

/* (un)hide splash wm.screen */
static void
hide(struct Splash *splash, int hide)
{
	if (hide)
		XUnmapWindow(wm.display, splash->obj.frame);
	else
		XMapWindow(wm.display, splash->obj.frame);
}

static void
raise(struct Splash *splash)
{
	TAILQ_REMOVE(&wm.stacking_order, &splash->obj, z_entry);
	TAILQ_INSERT_AFTER(
		&wm.stacking_order,
		&wm.layers[LAYER_NORMAL], &splash->obj,
		z_entry
	);
	XRestackWindows(wm.display, (Window[]){
		[0] = wm.layers[LAYER_NORMAL].frame,
		[1] = splash->obj.frame,
	}, 2);
}

static void
center(struct Monitor *mon, struct Splash *splash)
{
	fitmonitor(mon, &splash->geometry, 0.5);
	splash->geometry.x = mon->window_area.x
	                   + (mon->window_area.width - splash->geometry.width) / 2;
	splash->geometry.y = mon->window_area.y
	                   + (mon->window_area.height - splash->geometry.height) / 2;
	XMoveWindow(
		wm.display, splash->obj.frame,
		splash->geometry.x, splash->geometry.y
	);
	raise(splash);
}

static void
redecorate(struct Object *obj)
{
	struct Splash *splash = obj->self;

	updatepixmap(
		&splash->pixmap,
		&splash->pixwidth, &splash->pixheight,
		splash->geometry.width,
		splash->geometry.height
	);
	drawshadow(
		splash->pixmap,
		0, 0, splash->geometry.width, splash->geometry.height,
		UNFOCUSED
	);
	drawcommit(splash->pixmap, splash->obj.frame);
}

static void
manage(struct Object *tab, struct Monitor *mon, int desk, Window win,
       Window leader, XRectangle geometry, enum State state)
{
	struct Splash *splash;

	(void)tab;
	(void)leader;
	(void)state;
	splash = emalloc(sizeof(*splash));
	geometry.width += 2 * config.shadowthickness;
	geometry.height += 2 * config.shadowthickness;
	*splash = (struct Splash){
		.obj.win = win,
		.obj.self = splash,
		.obj.class = &splash_class,
		.geometry = geometry,
		.obj.frame = createframe(geometry),
		.pixmap = None,
		.pixwidth = 0,
		.pixheight = 0,
	};
	context_add(splash->obj.win, &splash->obj);
	context_add(splash->obj.frame, &splash->obj);
	redecorate(&splash->obj);
	XReparentWindow(
		wm.display, splash->obj.win, splash->obj.frame,
		config.shadowthickness, config.shadowthickness
	);
	XMapWindow(wm.display, splash->obj.win);
	XMapWindow(wm.display, splash->obj.frame);
	TAILQ_INSERT_HEAD(&managed_splashs, (struct Object *)splash, entry);
	TAILQ_INSERT_AFTER(
		&wm.stacking_order,
		&wm.layers[LAYER_NORMAL], &splash->obj,
		z_entry
	);
	splash->mon = mon;
	splash->desk = desk;
	center(mon, splash);
	hide(splash, REMOVE);
}

static void
unmanage(struct Object *obj)
{
	struct Splash *splash = obj->self;

	context_del(splash->obj.win);
	XDestroyWindow(wm.display, splash->obj.frame);
	XFreePixmap(wm.display, splash->pixmap);
	TAILQ_REMOVE(&managed_splashs, (struct Object *)splash, entry);
	TAILQ_REMOVE(&wm.stacking_order, &splash->obj, z_entry);
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
	raise(splash);
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
			center(wm.selmon, splash);
	}
}

static void
show_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry)
		hide(obj->self, True);
}

static void
hide_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_splashs, entry)
		hide(obj->self, True);
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
			hide(splash, REMOVE);
		} else if (splash->desk == desk_old) {
			hide(splash, ADD);
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
	.redecorate     = redecorate,
	.hide_desktop   = hide_desktop,
	.change_desktop = change_desktop,
};
