#include "shod.h"

struct Bar {
	struct Object obj;
	struct Monitor *mon;
	int strut[STRUT_LAST];                  /* strut values */
	Bool ispartial;                         /* whether strut has 12 elements rather than 4 */
	enum State state;
};

static struct Queue managed_bars;

/* fill strut array of bar */
static void
barstrut(struct Bar *bar)
{
	unsigned long *arr;
	unsigned long l, i;

	for (i = 0; i < STRUT_LAST; i++)
		bar->strut[i] = 0;
	bar->ispartial = 1;
	l = getcardsprop(dpy, bar->obj.win, atoms[_NET_WM_STRUT_PARTIAL], &arr);
	if (arr == NULL) {
		bar->ispartial = 0;
		l = getcardsprop(dpy, bar->obj.win, atoms[_NET_WM_STRUT], &arr);
		if (arr == NULL) {
			return;
		}
	}
	for (i = 0; i < STRUT_LAST && i < l; i++)
		bar->strut[i] = arr[i];
	XFree(arr);
}

static void
barstack(struct Bar *bar)
{
	Window wins[2];

	if (wm.focused != NULL && wm.focused->mon == bar->mon &&
	    wm.focused->state & FULLSCREEN)
		wins[0] = wm.focused->obj.win;
	else if (bar->state & BELOW)
		wins[0] = wm.layers[LAYER_DESK].obj.win;
	else
		wins[0] = wm.layers[LAYER_DOCK].obj.win;
	wins[1] = bar->obj.win;
	XRestackWindows(dpy, wins, 2);
}

static void
restack(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_bars, entry)
		barstack((struct Bar *)obj);
}

void
shoddocks(void)
{
	struct Object *obj;
	Window *wins;
	int nwins, ndocks;

	ndocks = 1;     /* +1 for the internal dock */
	TAILQ_FOREACH(obj, &managed_bars, entry)
		ndocks++;
	nwins = 0;
	wins = ecalloc(ndocks, sizeof(*wins));
	if (!TAILQ_EMPTY(&dock.dappq))
		wins[nwins++] = dock.obj.win;
	TAILQ_FOREACH(obj, &managed_bars, entry)
		wins[nwins++] = obj->win;
	XChangeProperty(
		dpy, root,
		atoms[_SHOD_DOCK_LIST],
		XA_WINDOW, 32,
		PropModeReplace,
		(void *)wins, nwins
	);
	free(wins);
}

static Bool
is_bar_at_mon(struct Monitor *mon, struct Bar *bar, int *l, int *r, int *t, int *b)
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

static void
monitor_reset(void)
{
	struct Monitor *mon;
	struct Object *obj;
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
		TAILQ_FOREACH(obj, &managed_bars, entry) {
			struct Bar *bar = (struct Bar *)obj;
			if (is_bar_at_mon(mon, bar, &l, &r, &t, &b))
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
}

static void
update_window_area(void)
{
	monitor_reset();
	container_class.monitor_reset();
}

static void
manage(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Bar *bar;

	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)rect;

	bar = emalloc(sizeof(*bar));
	*bar = (struct Bar){
		.obj.win = win,
		.obj.class = &bar_class,
		.state = state | MAXIMIZED,
		.mon = NULL,
	};
	context_add(win, &bar->obj);
	TAILQ_INSERT_TAIL(&managed_bars, (struct Object *)bar, entry);
	shoddocks();
	barstrut(bar);
	update_window_area();
	barstack(bar);
	XMapWindow(dpy, win);
}

static void
unmanage(struct Object *obj)
{
	struct Bar *bar = (struct Bar *)obj;

	context_del(obj->win);
	TAILQ_REMOVE(&managed_bars, (struct Object *)bar, entry);
	shoddocks();
	free(bar);
	update_window_area();
}

static void
toggleabove(struct Bar *bar)
{
	Window wins[2] = {wm.layers[LAYER_DOCK].obj.win, bar->obj.win};

	XRestackWindows(dpy, wins, 2);
	bar->state &= ~BELOW;
	bar->state ^= ABOVE;
}

static void
togglebelow(struct Bar *bar)
{
	Window wins[2] = {wm.layers[LAYER_DESK].obj.win, bar->obj.win};

	if (bar->state & BELOW)         /* bar is below; move it back to above */
		wins[0] = wm.layers[LAYER_DOCK].obj.win;
	XRestackWindows(dpy, wins, 2);
	bar->state &= ~ABOVE;
	bar->state ^= BELOW;
}

static void
togglemaximized(struct Bar *bar)
{
	bar->state ^= MAXIMIZED;
}

static void
toggleminimized(struct Bar *bar)
{
	Window win;

	win = bar->obj.win;
	if (bar->state & MINIMIZED)
		mapwin(win);
	else
		unmapwin(win);
	bar->state ^= MINIMIZED;
}

static void
changestate(struct Object *obj, enum State mask, int set)
{
	static struct {
		enum State state;
		void (*fun)(struct Bar *);
	} togglers[] = {
		{ ABOVE,        &toggleabove     },
		{ BELOW,        &togglebelow     },
		{ MAXIMIZED,    &togglemaximized },
		{ MINIMIZED,    &toggleminimized },
	};
	enum State state;
	struct Bar *bar;
	size_t i;

	bar = (struct Bar *)obj;
	for (i = 0; i < LEN(togglers); i++) {
		state = togglers[i].state;
		if (!(mask & state))
			continue;       /* state change not requested */
		if (set == REMOVE && !(bar->state & state))
			continue;       /* state already unset */
		if (set == ADD    && !(bar->state & state))
			continue;       /* state already set */
		(*togglers[i].fun)(bar);
	}
	update_window_area();
}

static void
init(void)
{
	TAILQ_INIT(&managed_bars);
}

static void
clean(void)
{
	struct Object *obj;

	while ((obj = TAILQ_FIRST(&managed_bars)) != NULL)
		unmanage(obj);
}

static void
monitor_delete(struct Monitor *mon)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_bars, entry) {
		struct Bar *bar = (struct Bar *)obj;
		if (bar->mon == mon)
			bar->mon = NULL;
	}
}

static void
handle_property(struct Object *self, Atom property)
{
	struct Bar *bar = (struct Bar *)self;

	if (property == _NET_WM_STRUT_PARTIAL || property == _NET_WM_STRUT) {
		barstrut(bar);
		update_window_area();
	}
}

struct Class bar_class = {
	.setstate       = changestate,
	.manage         = manage,
	.unmanage       = unmanage,
	.btnpress       = NULL,
	.init           = init,
	.clean          = clean,
	.monitor_delete = monitor_delete,
	.monitor_reset  = monitor_reset,
	.handle_property = handle_property,
	.restack        = restack,
};
