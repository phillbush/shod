#include "shod.h"

struct Notification {
	struct Object obj;
	Window frame;                           /* window to reparent the actual client window */
	Pixmap pix;                             /* pixmap to draw the frame */
	int w, h;                               /* geometry of the entire thing (content + decoration) */
	int pw, ph;                             /* pixmap width and height */
};

static struct Queue managed_notifications;

static void
notifdecorate(struct Notification *n)
{
	/* (re)create pixmap */
	if (n->pw != n->w || n->ph != n->h || n->pix == None) {
		if (n->pix != None)
			XFreePixmap(dpy, n->pix);
		n->pix = XCreatePixmap(dpy, n->frame, n->w, n->h, depth);
	}
	n->pw = n->w;
	n->ph = n->h;

	drawborders(n->pix, n->w, n->h, FOCUSED);

	drawcommit(n->pix, n->frame);
}

static void
monitor_reset(void)
{
	struct Object *n;
	struct Notification *notif;
	int x, y, h;

	h = 0;
	TAILQ_FOREACH(n, &managed_notifications, entry) {
		notif = n->self;
		x = wm.monitors[0]->wx;
		y = wm.monitors[0]->wy;
		switch (config.notifgravity[0]) {
		case 'N':
			switch (config.notifgravity[1]) {
			case 'W':
				break;
			case 'E':
				x += wm.monitors[0]->ww - notif->w;
				break;
			default:
				x += (wm.monitors[0]->ww - notif->w) / 2;
				break;
			}
			break;
		case 'S':
			switch(config.notifgravity[1]) {
			case 'W':
				y += wm.monitors[0]->wh - notif->h;
				break;
			case 'E':
				x += wm.monitors[0]->ww - notif->w;
				y += wm.monitors[0]->wh - notif->h;
				break;
			default:
				x += (wm.monitors[0]->ww - notif->w) / 2;
				y += wm.monitors[0]->wh - notif->h;
				break;
			}
			break;
		case 'W':
			y += (wm.monitors[0]->wh - notif->h) / 2;
			break;
		case 'C':
			x += (wm.monitors[0]->ww - notif->w) / 2;
			y += (wm.monitors[0]->wh - notif->h) / 2;
			break;
		case 'E':
			x += wm.monitors[0]->ww - notif->w;
			y += (wm.monitors[0]->wh - notif->h) / 2;
			break;
		default:
			x += wm.monitors[0]->ww - notif->w;
			break;
		}

		if (config.notifgravity[0] == 'S')
			y -= h;
		else
			y += h;
		h += notif->h + config.notifgap + config.borderwidth * 2;

		XMoveResizeWindow(dpy, notif->frame, x, y, notif->w, notif->h);
		XMoveResizeWindow(dpy, notif->obj.win, config.borderwidth, config.borderwidth, notif->w - 2 * config.borderwidth, notif->h - 2 * config.borderwidth);
		XMapWindow(dpy, notif->frame);
		if (notif->pw != notif->w || notif->ph != notif->h) {
			notifdecorate(notif);
		}
		window_configure_notify(
			dpy, notif->obj.win,
			x + config.borderwidth,
			y + config.borderwidth,
			notif->w - 2 * config.borderwidth,
			notif->h - 2 * config.borderwidth
		);
	}
}

static void
manage(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Notification *notif;

	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)state;
	notif = emalloc(sizeof(*notif));
	*notif = (struct Notification){
		.w = rect.width + 2 * config.borderwidth,
		.h = rect.height + 2 * config.borderwidth,
		.pix = None,
		.obj.win = win,
		.obj.self = notif,
		.obj.class = &notif_class,
	};
	context_add(win, &notif->obj);
	TAILQ_INSERT_TAIL(&managed_notifications, (struct Object *)notif, entry);
	notif->frame = createframe((XRectangle){0, 0, 1, 1});
	XReparentWindow(dpy, notif->obj.win, notif->frame, 0, 0);
	XMapWindow(dpy, notif->obj.win);
	monitor_reset();
}

static void
unmanage(struct Object *obj)
{
	struct Notification *notif = obj->self;

	context_del(obj->win);
	TAILQ_REMOVE(&managed_notifications, (struct Object *)notif, entry);
	if (notif->pix != None)
		XFreePixmap(dpy, notif->pix);
	XReparentWindow(dpy, notif->obj.win, root, 0, 0);
	XDestroyWindow(dpy, notif->frame);
	free(notif);
	monitor_reset();
}

static void
init(void)
{
	TAILQ_INIT(&managed_notifications);
}

static void
clean(void)
{
	struct Object *obj;

	while ((obj = TAILQ_FIRST(&managed_notifications)) != NULL)
		unmanage(obj);
}

static void
redecorate_all(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_notifications, entry)
		notifdecorate(obj->self);
}

struct Class notif_class = {
	.setstate       = NULL,
	.manage         = manage,
	.unmanage       = unmanage,
	.init           = init,
	.clean          = clean,
	.monitor_reset  = monitor_reset,
	.redecorate_all = redecorate_all,
};
