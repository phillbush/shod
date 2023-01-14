#include "shod.h"

#include <X11/extensions/Xrandr.h>

void
moninit(void)
{
	int i;

	if ((wm.xrandr = XRRQueryExtension(dpy, &wm.xrandrev, &i))) {
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	}
}

void
monevent(XEvent *e)
{
	XRRScreenChangeNotifyEvent *ev;

	ev = (XRRScreenChangeNotifyEvent *)e;
	if (ev->root == root) {
		(void)XRRUpdateConfiguration(e);
		monupdate();
		monupdatearea();
		notifplace();
		dockupdate();
	}
}

/* delete monitor and set monitor of clients on it to NULL */
void
mondel(struct Monitor *mon)
{
	struct Object *obj;
	struct Container *c;

	TAILQ_REMOVE(&wm.monq, mon, entry);
	TAILQ_FOREACH(c, &wm.focusq, entry)
		if (c->mon == mon)
			c->mon = NULL;
	TAILQ_FOREACH(obj, &wm.menuq, entry)
		if (((struct Menu *)obj)->mon == mon)
			((struct Menu *)obj)->mon = NULL;
	TAILQ_FOREACH(obj, &wm.splashq, entry)
		if (((struct Splash *)obj)->mon == mon)
			((struct Splash *)obj)->mon = NULL;
	free(mon);
}

/* get monitor given coordinates */
struct Monitor *
getmon(int x, int y)
{
	struct Monitor *mon;

	TAILQ_FOREACH(mon, &wm.monq, entry)
		if (x >= mon->mx && x < mon->mx + mon->mw && y >= mon->my && y < mon->my + mon->mh)
			return mon;
	return NULL;
}

/* update the list of monitors */
void
monupdate(void)
{
	XRRScreenResources *sr;
	XRRCrtcInfo *ci;
	struct MonitorQueue monq;               /* queue of monitors */
	struct Monitor *mon;
	struct Container *c, *focus;
	struct Object *m, *s;
	int delselmon, i;

	TAILQ_INIT(&monq);
	sr = XRRGetScreenResources(dpy, root);
	for (i = 0, ci = NULL; i < sr->ncrtc; i++) {
		if ((ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i])) == NULL)
			continue;
		if (ci->noutput == 0)
			goto next;

		TAILQ_FOREACH(mon, &wm.monq, entry)
			if (ci->x == mon->mx && ci->y == mon->my &&
			    (int)ci->width == mon->mw && (int)ci->height == mon->mh)
				break;
		if (mon != NULL) {
			TAILQ_REMOVE(&wm.monq, mon, entry);
		} else {
			mon = emalloc(sizeof(*mon));
			*mon = (struct Monitor){
				.mx = ci->x,
				.wx = ci->x,
				.my = ci->y,
				.wy = ci->y,
				.mw = ci->width,
				.ww = ci->width,
				.mh = ci->height,
				.wh = ci->height,
			};
		}
		TAILQ_INSERT_TAIL(&monq, mon, entry);
next:
		XRRFreeCrtcInfo(ci);
	}
	XRRFreeScreenResources(sr);

	/* delete monitors that do not exist anymore */
	delselmon = 0;
	while ((mon = TAILQ_FIRST(&wm.monq)) != NULL) {
		if (mon == wm.selmon)
			delselmon = 1;
		mondel(mon);
	}
	if (delselmon) {
		wm.selmon = TAILQ_FIRST(&wm.monq);
	}

	/* commit new list of monitor */
	while ((mon = TAILQ_FIRST(&monq)) != NULL) {
		TAILQ_REMOVE(&monq, mon, entry);
		TAILQ_INSERT_TAIL(&wm.monq, mon, entry);
	}

	/* send containers which do not belong to a monitor to selected desktop */
	focus = NULL;
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (!c->isminimized && c->mon == NULL) {
			focus = c;
			c->mon = wm.selmon;
			c->desk = wm.selmon->seldesk;
			containerplace(c, wm.selmon, wm.selmon->seldesk, 0);
			containermoveresize(c, 0);
			ewmhsetwmdesktop(c);
			ewmhsetstate(c);
		}
	}
	TAILQ_FOREACH(m, &wm.menuq, entry)
		if (((struct Menu *)m)->mon == NULL)
			menuplace(wm.selmon, (struct Menu *)m);
	TAILQ_FOREACH(s, &wm.splashq, entry)
		if (((struct Splash *)s)->mon == NULL)
			splashplace(wm.selmon, (struct Splash *)s);
	if (focus != NULL)              /* if a contained changed desktop, focus it */
		tabfocus(focus->selcol->selrow->seltab, 1);
	wm.setclientlist = 1;
}

/* update window area and dock area of monitor */
void
monupdatearea(void)
{
	struct Monitor *mon;
	struct Bar *bar;
	struct Object *p;
	struct Container *c;
	int t, b, l, r;

	TAILQ_FOREACH(mon, &wm.monq, entry) {
		mon->wx = mon->mx;
		mon->wy = mon->my;
		mon->ww = mon->mw;
		mon->wh = mon->mh;
		t = b = l = r = 0;
		if (mon == TAILQ_FIRST(&wm.monq) && dock.mapped) {
			switch (config.dockgravity[0]) {
			case 'N':
				t = config.dockwidth;
				break;
			case 'S':
				b = config.dockwidth;
				break;
			case 'W':
				l = config.dockwidth;
				break;
			case 'E':
			default:
				r = config.dockwidth;
				break;
			}
		}
		TAILQ_FOREACH(p, &wm.barq, entry) {
			bar = (struct Bar *)p;
			if (bar->strut[STRUT_TOP] != 0) {
				if (bar->strut[STRUT_TOP] >= mon->my &&
				    bar->strut[STRUT_TOP] < mon->my + mon->mh &&
				    (!bar->ispartial ||
				     (bar->strut[STRUT_TOP_START_X] >= mon->mx &&
				     bar->strut[STRUT_TOP_END_X] <= mon->mx + mon->mw))) {
					t = max(t, bar->strut[STRUT_TOP] - mon->my);
				}
			} else if (bar->strut[STRUT_BOTTOM] != 0) {
				if (DisplayHeight(dpy, screen) - bar->strut[STRUT_BOTTOM] <= mon->my + mon->mh &&
				    DisplayHeight(dpy, screen) - bar->strut[STRUT_BOTTOM] > mon->my &&
				    (!bar->ispartial ||
				     (bar->strut[STRUT_BOTTOM_START_X] >= mon->mx &&
				     bar->strut[STRUT_BOTTOM_END_X] <= mon->mx + mon->mw))) {
					b = max(b, bar->strut[STRUT_BOTTOM] - (DisplayHeight(dpy, screen) - (mon->my + mon->mh)));
				}
			} else if (bar->strut[STRUT_LEFT] != 0) {
				if (bar->strut[STRUT_LEFT] >= mon->mx &&
				    bar->strut[STRUT_LEFT] < mon->mx + mon->mw &&
				    (!bar->ispartial ||
				     (bar->strut[STRUT_LEFT_START_Y] >= mon->my &&
				     bar->strut[STRUT_LEFT_END_Y] <= mon->my + mon->mh))) {
					l = max(l, bar->strut[STRUT_LEFT] - mon->mx);
				}
			} else if (bar->strut[STRUT_RIGHT] != 0) {
				if (DisplayWidth(dpy, screen) - bar->strut[STRUT_RIGHT] <= mon->mx + mon->mw &&
				    DisplayWidth(dpy, screen) - bar->strut[STRUT_RIGHT] > mon->mx &&
				    (!bar->ispartial ||
				     (bar->strut[STRUT_RIGHT_START_Y] >= mon->my &&
				     bar->strut[STRUT_RIGHT_END_Y] <= mon->my + mon->mh))) {
					r = max(r, bar->strut[STRUT_RIGHT] - (DisplayWidth(dpy, screen) - (mon->mx + mon->mw)));
				}
			}
		}
		mon->wy += t;
		mon->wh -= t + b;
		mon->wx += l;
		mon->ww -= l + r;
	}
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (c->ismaximized) {
			containercalccols(c, 0, 1);
			containermoveresize(c, 0);
			containerredecorate(c, NULL, NULL, 0);
		}
	}
	wm.setclientlist = 1;
}

/* if window is bigger than monitor, resize it while maintaining proportion */
void
fitmonitor(struct Monitor *mon, int *x, int *y, int *w, int *h, float factor)
{
	int origw, origh;
	int minw, minh;

	origw = *w;
	origh = *h;
	minw = min(origw, mon->ww * factor);
	minh = min(origh, mon->wh * factor);
	if (origw * minh > origh * minw) {
		minh = (origh * minw) / origw;
		minw = (origw * minh) / origh;
	} else {
		minw = (origw * minh) / origh;
		minh = (origh * minw) / origw;
	}
	*w = max(wm.minsize, minw);
	*h = max(wm.minsize, minh);
	*x = max(mon->wx, min(mon->wx + mon->ww - *w, *x));
	*y = max(mon->wy, min(mon->wy + mon->wh - *h, *y));
}
