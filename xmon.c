#include "shod.h"

#include <X11/extensions/Xrandr.h>

void
moninit(void)
{
	int i;

	if ((wm.xrandr = XRRQueryExtension(dpy, &wm.xrandrev, &i)))
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	wm.xrandrev += RRScreenChangeNotify;
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
	TAILQ_FOREACH(obj, &wm.barq, entry)
		if (((struct Bar *)obj)->mon == mon)
			((struct Bar *)obj)->mon = NULL;
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

/* return if bar is on monitor and pixels bar uses at each edge */
static Bool
baratmon(struct Monitor *mon, struct Bar *bar, int *l, int *r, int *t, int *b)
{
	int strutl, strutr, strutt, strutb;
	Bool atmon;

	if (l != NULL)
		*l = 0;
	if (r != NULL)
		*r = 0;
	if (t != NULL)
		*t = 0;
	if (b != NULL)
		*b = 0;
	if (bar->state & MINIMIZED)
		return False;
	if (!(bar->state & MAXIMIZED))
		return False;
	atmon = False;
	strutl = bar->strut[STRUT_LEFT];
	strutr = DisplayWidth(dpy, screen) - bar->strut[STRUT_RIGHT];
	strutt = bar->strut[STRUT_TOP];
	strutb = DisplayHeight(dpy, screen) - bar->strut[STRUT_BOTTOM];
	if (strutt > 0 && strutt >= mon->my && strutt < mon->my + mon->mh &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_TOP_START_X] >= mon->mx &&
	     bar->strut[STRUT_TOP_END_X] <= mon->mx + mon->mw))) {
		if (t != NULL) {
			*t = bar->strut[STRUT_TOP] - mon->my;
		}
		atmon = True;
	}
	if (strutb > 0 && strutb <= mon->my + mon->mh && strutb > mon->my &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_BOTTOM_START_X] >= mon->mx &&
	     bar->strut[STRUT_BOTTOM_END_X] <= mon->mx + mon->mw))) {
		if (b != NULL) {
			*b = bar->strut[STRUT_BOTTOM];
			*b -= DisplayHeight(dpy, screen);
			*b += mon->my + mon->mh;
		}
		atmon = True;
	}
	if (strutl > 0 && strutl >= mon->mx && strutl < mon->mx + mon->mw &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_LEFT_START_Y] >= mon->my &&
	     bar->strut[STRUT_LEFT_END_Y] <= mon->my + mon->mh))) {
		if (l != NULL) {
			*l = bar->strut[STRUT_LEFT] - mon->mx;
		}
		atmon = True;
	}
	if (strutr > 0 && strutr <= mon->mx + mon->mw && strutr > mon->mx &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_RIGHT_START_Y] >= mon->my &&
	     bar->strut[STRUT_RIGHT_END_Y] <= mon->my + mon->mh))) {
		if (r != NULL) {
			*r = bar->strut[STRUT_RIGHT];
			*r -= DisplayWidth(dpy, screen);
			*r += mon->mx + mon->mw;
		}
		atmon = True;
	}
	return atmon;
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
		if (mon == wm.selmon) {
			delselmon = 1;
			wm.selmon = NULL;
		}
		mondel(mon);
	}
	if (delselmon) {
		wm.selmon = TAILQ_FIRST(&monq);
	}

	/* commit new list of monitor */
	while ((mon = TAILQ_FIRST(&monq)) != NULL) {
		TAILQ_REMOVE(&monq, mon, entry);
		TAILQ_INSERT_TAIL(&wm.monq, mon, entry);
	}

	/* send containers which do not belong to a monitor to selected desktop */
	focus = NULL;
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (!(c->state & MINIMIZED) && c->mon == NULL) {
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
	int l, r, t, b;
	int left, right, top, bottom;

	TAILQ_FOREACH(mon, &wm.monq, entry) {
		mon->wx = mon->mx;
		mon->wy = mon->my;
		mon->ww = mon->mw;
		mon->wh = mon->mh;
		left = right = top = bottom = 0;
		if (mon == TAILQ_FIRST(&wm.monq) && !TAILQ_EMPTY(&dock.dappq) &&
		    (dock.state & MAXIMIZED) && !(dock.state & MINIMIZED)) {
			switch (config.dockgravity[0]) {
			case 'N':
				top = config.dockwidth;
				break;
			case 'S':
				bottom = config.dockwidth;
				break;
			case 'W':
				left = config.dockwidth;
				break;
			case 'E':
			default:
				right = config.dockwidth;
				break;
			}
		}
		TAILQ_FOREACH(p, &wm.barq, entry) {
			bar = (struct Bar *)p;
			if (baratmon(mon, bar, &l, &r, &t, &b))
				bar->mon = mon;
			left   = max(left, l);
			right  = max(right, r);
			top    = max(top, t);
			bottom = max(bottom, b);
		}
		mon->wy += top;
		mon->wh -= top + bottom;
		mon->wx += left;
		mon->ww -= left + right;
	}
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (c->state & MAXIMIZED) {
			containercalccols(c);
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
