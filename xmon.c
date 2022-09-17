#include "shod.h"

#include <X11/extensions/Xinerama.h>

/* check if monitor geometry is unique */
static int
monisuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}

/* add monitor */
static void
monnew(XineramaScreenInfo *info)
{
	struct Monitor *mon;

	mon = emalloc(sizeof *mon);
	*mon = (struct Monitor){
		.mx = info->x_org,
		.my = info->y_org,
		.mw = info->width,
		.mh = info->height,
		.wx = info->x_org,
		.wy = info->y_org,
		.ww = info->width,
		.wh = info->height,
	};
	mon->seldesk = 0;
	TAILQ_INSERT_TAIL(&wm.monq, mon, entry);
}

/* delete monitor and set monitor of clients on it to NULL */
void
mondel(struct Monitor *mon)
{
	struct Container *c;

	TAILQ_REMOVE(&wm.monq, mon, entry);
	TAILQ_FOREACH(c, &wm.focusq, entry)
		if (c->mon == mon)
			c->mon = NULL;
	free(mon);
}

/* get monitor given coordinates */
struct Monitor *
getmon(int x, int y)
{
	struct Monitor *mon;

	TAILQ_FOREACH(mon, &wm.monq, entry)
		if (x >= mon->mx && x <= mon->mx + mon->mw && y >= mon->my && y <= mon->my + mon->mh)
			return mon;
	return NULL;
}

/* update the list of monitors */
void
monupdate(void)
{
	XineramaScreenInfo *info = NULL;
	XineramaScreenInfo *unique = NULL;
	struct Monitor *mon, *tmp;
	struct Container *c, *focus;
	struct Object *t, *m, *s;
	int delselmon = 0;
	int del, add;
	int i, j, n;
	int moncount;

	info = XineramaQueryScreens(dpy, &n);
	unique = ecalloc(n, sizeof *unique);
	
	/* only consider unique geometries as separate screens */
	for (i = 0, j = 0; i < n; i++)
		if (monisuniquegeom(unique, j, &info[i]))
			memcpy(&unique[j++], &info[i], sizeof *unique);
	XFree(info);
	moncount = j;

	/* look for monitors that do not exist anymore and delete them */
	mon = TAILQ_FIRST(&wm.monq);
	while (mon != NULL) {
		del = 1;
		for (i = 0; i < moncount; i++) {
			if (unique[i].x_org == mon->mx && unique[i].y_org == mon->my &&
			    unique[i].width == mon->mw && unique[i].height == mon->mh) {
				del = 0;
				break;
			}
		}
		tmp = mon;
		mon = TAILQ_NEXT(mon, entry);
		if (del) {
			if (tmp == wm.selmon)
				delselmon = 1;
			mondel(tmp);
		}
	}

	/* look for new monitors and add them */
	for (i = 0; i < moncount; i++) {
		add = 1;
		TAILQ_FOREACH(mon, &wm.monq, entry) {
			if (unique[i].x_org == mon->mx && unique[i].y_org == mon->my &&
			    unique[i].width == mon->mw && unique[i].height == mon->mh) {
				add = 0;
				break;
			}
		}
		if (add) {
			monnew(&unique[i]);
		}
	}
	if (delselmon)
		wm.selmon = TAILQ_FIRST(&wm.monq);

	/* send containers which do not belong to a window to selected desktop */
	focus = NULL;
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (!c->isminimized && c->mon == NULL) {
			focus = c;
			c->mon = wm.selmon;
			c->desk = wm.selmon->seldesk;
			containerplace(c, wm.selmon, wm.selmon->seldesk, 0);
			containermoveresize(c, 0);

			/* move menus to new monitor */
			TAB_FOREACH_BEGIN(c, t) {
				TAILQ_FOREACH(m, &((struct Tab *)t)->menuq, entry) {
					menuplace((struct Menu *)m);
				}
			} TAB_FOREACH_END

			ewmhsetwmdesktop(c);
			ewmhsetstate(c);
		}
	}
	TAILQ_FOREACH(s, &wm.splashq, entry)
		splashplace((struct Splash *)s);
	if (focus != NULL)              /* if a contained changed desktop, focus it */
		tabfocus(focus->selcol->selrow->seltab, 1);
	ewmhsetclientsstacking();
	free(unique);
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
	ewmhsetclientsstacking();
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
