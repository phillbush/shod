#include "shod.h"

/* fill strut array of bar */
void
barstrut(struct Bar *bar)
{
	unsigned long *arr;
	unsigned long l, i;

	for (i = 0; i < STRUT_LAST; i++)
		bar->strut[i] = 0;
	bar->ispartial = 1;
	l = getcardsprop(bar->obj.win, atoms[_NET_WM_STRUT_PARTIAL], &arr);
	if (arr == NULL) {
		bar->ispartial = 0;
		l = getcardsprop(bar->obj.win, atoms[_NET_WM_STRUT], &arr);
		if (arr == NULL) {
			return;
		}
	}
	for (i = 0; i < STRUT_LAST && i < l; i++)
		bar->strut[i] = arr[i];
	XFree(arr);
}

void
barstack(struct Bar *bar)
{
	Window wins[2];

	if (wm.focused != NULL && wm.focused->mon == bar->mon &&
	    wm.focused->state & FULLSCREEN)
		wins[0] = wm.focused->frame;
	else if (bar->state & BELOW)
		wins[0] = wm.layers[LAYER_DESK].frame;
	else
		wins[0] = wm.layers[LAYER_DOCK].frame;
	wins[1] = bar->obj.win;
	XRestackWindows(dpy, wins, 2);
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
		.obj.class = bar_class,
		.state = state | MAXIMIZED,
		.mon = NULL,
	};
	context_add(win, &bar->obj);
	TAILQ_INSERT_TAIL(&wm.barq, (struct Object *)bar, entry);
	shoddocks();
	barstrut(bar);
	monupdatearea();
	barstack(bar);
	XMapWindow(dpy, win);
}

static void
unmanage(struct Object *obj)
{
	struct Bar *bar = (struct Bar *)obj;

	context_del(obj->win);
	TAILQ_REMOVE(&wm.barq, (struct Object *)bar, entry);
	shoddocks();
	free(bar);
	monupdatearea();
}

static void
toggleabove(struct Bar *bar)
{
	Window wins[2] = {wm.layers[LAYER_DOCK].frame, bar->obj.win};

	XRestackWindows(dpy, wins, 2);
	bar->state &= ~BELOW;
	bar->state ^= ABOVE;
}

static void
togglebelow(struct Bar *bar)
{
	Window wins[2] = {wm.layers[LAYER_DESK].frame, bar->obj.win};

	if (bar->state & BELOW)         /* bar is below; move it back to above */
		wins[0] = wm.layers[LAYER_DOCK].frame;
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
	monupdatearea();
}

struct Class *bar_class = &(struct Class){
	.type           = TYPE_BAR,
	.setstate       = changestate,
	.manage         = manage,
	.unmanage       = unmanage,
};
