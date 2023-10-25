#include "shod.h"

/* create notification window */
static void
notifnew(Window win, int w, int h)
{
	struct Notification *notif;

	notif = emalloc(sizeof(*notif));
	*notif = (struct Notification){
		.w = w + 2 * config.borderwidth,
		.h = h + 2 * config.borderwidth,
		.pix = None,
		.obj.class = notif_class,
		.obj.win = win,
	};
	TAILQ_INSERT_TAIL(&wm.notifq, (struct Object *)notif, entry);
	notif->frame = XCreateWindow(
		dpy, root, 0, 0, 1, 1, 0,
		depth, CopyFromParent, visual,
		clientmask,
		&(XSetWindowAttributes){
			.event_mask = SubstructureNotifyMask | SubstructureRedirectMask,
			.colormap = colormap
		}
	);
	XReparentWindow(dpy, notif->obj.win, notif->frame, 0, 0);
	XMapWindow(dpy, notif->obj.win);
}

/* decorate notification */
void
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

/* place notifications */
void
notifplace(void)
{
	struct Object *n;
	struct Notification *notif;
	int x, y, h;

	h = 0;
	TAILQ_FOREACH(n, &wm.notifq, entry) {
		notif = (struct Notification *)n;
		x = TAILQ_FIRST(&wm.monq)->wx;
		y = TAILQ_FIRST(&wm.monq)->wy;
		switch (config.notifgravity[0]) {
		case 'N':
			switch (config.notifgravity[1]) {
			case 'W':
				break;
			case 'E':
				x += TAILQ_FIRST(&wm.monq)->ww - notif->w;
				break;
			default:
				x += (TAILQ_FIRST(&wm.monq)->ww - notif->w) / 2;
				break;
			}
			break;
		case 'S':
			switch(config.notifgravity[1]) {
			case 'W':
				y += TAILQ_FIRST(&wm.monq)->wh - notif->h;
				break;
			case 'E':
				x += TAILQ_FIRST(&wm.monq)->ww - notif->w;
				y += TAILQ_FIRST(&wm.monq)->wh - notif->h;
				break;
			default:
				x += (TAILQ_FIRST(&wm.monq)->ww - notif->w) / 2;
				y += TAILQ_FIRST(&wm.monq)->wh - notif->h;
				break;
			}
			break;
		case 'W':
			y += (TAILQ_FIRST(&wm.monq)->wh - notif->h) / 2;
			break;
		case 'C':
			x += (TAILQ_FIRST(&wm.monq)->ww - notif->w) / 2;
			y += (TAILQ_FIRST(&wm.monq)->wh - notif->h) / 2;
			break;
		case 'E':
			x += TAILQ_FIRST(&wm.monq)->ww - notif->w;
			y += (TAILQ_FIRST(&wm.monq)->wh - notif->h) / 2;
			break;
		default:
			x += TAILQ_FIRST(&wm.monq)->ww - notif->w;
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
		winnotify(notif->obj.win, x + config.borderwidth, y + config.borderwidth, notif->w - 2 * config.borderwidth, notif->h - 2 * config.borderwidth);
	}
}

/* add notification window into notification queue; and update notification placement */
void
managenotif(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int state)
{
	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)state;
	notifnew(win, rect.width, rect.height);
	notifplace();
}

/* delete notification */
int
unmanagenotif(struct Object *obj)
{
	struct Notification *notif;

	notif = (struct Notification *)obj;
	TAILQ_REMOVE(&wm.notifq, (struct Object *)notif, entry);
	if (notif->pix != None)
		XFreePixmap(dpy, notif->pix);
	XReparentWindow(dpy, notif->obj.win, root, 0, 0);
	XDestroyWindow(dpy, notif->frame);
	free(notif);
	notifplace();
	return 0;
}

Class *notif_class = &(Class){
	.type           = TYPE_NOTIFICATION,
	.setstate       = NULL,
};
