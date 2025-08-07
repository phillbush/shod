#include "shod.h"

enum {
	/* strut elements array indices */
	STRUT_LEFT              = 0,
	STRUT_RIGHT             = 1,
	STRUT_TOP               = 2,
	STRUT_BOTTOM            = 3,
	STRUT_LEFT_START_Y      = 4,
	STRUT_LEFT_END_Y        = 5,
	STRUT_RIGHT_START_Y     = 6,
	STRUT_RIGHT_END_Y       = 7,
	STRUT_TOP_START_X       = 8,
	STRUT_TOP_END_X         = 9,
	STRUT_BOTTOM_START_X    = 10,
	STRUT_BOTTOM_END_X      = 11,
	STRUT_LAST              = 12,
};

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
	long *arr;
	long l, i;

	for (i = 0; i < STRUT_LAST; i++)
		bar->strut[i] = 0;
	bar->ispartial = 1;
	l = getcardsprop(dpy, bar->obj.win, atoms[_NET_WM_STRUT_PARTIAL], &arr);
	if (l != 12) {
		bar->ispartial = 0;
		l = getcardsprop(dpy, bar->obj.win, atoms[_NET_WM_STRUT], &arr);
		if (l != 4)
			goto error;
	}
	for (i = 0; i < STRUT_LAST && i < l; i++)
		bar->strut[i] = arr[i];
error:
	XFree(arr);
}

static void
barstack(struct Bar *bar)
{
	Window wins[2];

	if (bar->state & BELOW)
		wins[0] = wm.layertop[LAYER_DESK];
	else
		wins[0] = wm.layertop[LAYER_DOCK];
	wins[1] = bar->obj.win;
	XRestackWindows(dpy, wins, 2);
}

static void
restack(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &managed_bars, entry)
		barstack(obj->self);
}

void
shoddocks(void)
{
	struct Object *obj;
	Window *wins;
	int nwins, ndocks;

	ndocks = 0;
	TAILQ_FOREACH(obj, &managed_bars, entry)
		ndocks++;
	nwins = 0;
	wins = ecalloc(ndocks, sizeof(*wins));
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
	if (strutt > 0 && strutt >= mon->geometry.y && strutt < mon->geometry.y + mon->geometry.height &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_TOP_START_X] >= mon->geometry.x &&
	     bar->strut[STRUT_TOP_END_X] <= mon->geometry.x + mon->geometry.width))) {
		if (t != NULL) {
			*t = bar->strut[STRUT_TOP] - mon->geometry.y;
		}
		atmon = True;
	}
	if (strutb > 0 && strutb <= mon->geometry.y + mon->geometry.height && strutb > mon->geometry.y &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_BOTTOM_START_X] >= mon->geometry.x &&
	     bar->strut[STRUT_BOTTOM_END_X] <= mon->geometry.x + mon->geometry.width))) {
		if (b != NULL) {
			*b = bar->strut[STRUT_BOTTOM];
			*b -= DisplayHeight(dpy, screen);
			*b += mon->geometry.y + mon->geometry.height;
		}
		atmon = True;
	}
	if (strutl > 0 && strutl >= mon->geometry.x && strutl < mon->geometry.x + mon->geometry.width &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_LEFT_START_Y] >= mon->geometry.y &&
	     bar->strut[STRUT_LEFT_END_Y] <= mon->geometry.y + mon->geometry.height))) {
		if (l != NULL) {
			*l = bar->strut[STRUT_LEFT] - mon->geometry.x;
		}
		atmon = True;
	}
	if (strutr > 0 && strutr <= mon->geometry.x + mon->geometry.width && strutr > mon->geometry.x &&
	    (!bar->ispartial ||
	     (bar->strut[STRUT_RIGHT_START_Y] >= mon->geometry.y &&
	     bar->strut[STRUT_RIGHT_END_Y] <= mon->geometry.y + mon->geometry.height))) {
		if (r != NULL) {
			*r = bar->strut[STRUT_RIGHT];
			*r -= DisplayWidth(dpy, screen);
			*r += mon->geometry.x + mon->geometry.width;
		}
		atmon = True;
	}
	return atmon;
}

static void
monitor_reset(void)
{
	for (int i = 0; i < wm.nmonitors; i++) {
		struct Monitor *mon = wm.monitors[i];
		struct Object *obj;
		int left, right, top, bottom;

		left = right = top = bottom = 0;
		TAILQ_FOREACH(obj, &managed_bars, entry) {
			struct Bar *bar = obj->self;
			int l, r, t, b;

			if (is_bar_at_mon(mon, bar, &l, &r, &t, &b))
				bar->mon = mon;
			left   = max(left, l);
			right  = max(right, r);
			top    = max(top, t);
			bottom = max(bottom, b);
		}
		mon->window_area = (XRectangle){
			.x = max(mon->window_area.x, mon->geometry.x + left),
			.y = max(mon->window_area.y, mon->geometry.y + top),
			.width = min(
				mon->window_area.width,
				mon->geometry.width - left - right
			),
			.height = min(
				mon->window_area.height,
				mon->geometry.height - top - bottom
			),
		};
		mon->window_area.width = max(mon->window_area.width, 1);
		mon->window_area.height = max(mon->window_area.height, 1);
	}
}

static void
update_window_area(void)
{
	monitor_reset();
	container_class.monitor_reset();
}

static void
manage(struct Object *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
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
		.obj.self = bar,
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
	struct Bar *bar = obj->self;

	context_del(obj->win);
	TAILQ_REMOVE(&managed_bars, (struct Object *)bar, entry);
	shoddocks();
	free(bar);
	update_window_area();
}

static void
toggleabove(struct Bar *bar)
{
	bar->state &= ~BELOW;
	bar->state ^= ABOVE;
	barstack(bar);
}

static void
togglebelow(struct Bar *bar)
{
	bar->state &= ~ABOVE;
	bar->state ^= BELOW;
	barstack(bar);
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
		XMapWindow(dpy, win);
	else
		XUnmapWindow(dpy, win);
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

	bar = obj->self;
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
		struct Bar *bar = obj->self;
		if (bar->mon == mon)
			bar->mon = NULL;
	}
}

static void
handle_property(struct Object *self, Atom property)
{
	struct Bar *bar = self->self;

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
