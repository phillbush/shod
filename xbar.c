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

/* map bar window */
void
managebar(struct Tab *tab, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, int state, int ignoreunmap)
{
	struct Bar *bar;

	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)rect;
	(void)state;
	(void)ignoreunmap;
	Window wins[2] = {wm.layers[LAYER_DOCK].frame, win};

	bar = emalloc(sizeof(*bar));
	*bar = (struct Bar){
		.obj.win = win,
		.obj.class = bar_class,
	};
	TAILQ_INSERT_HEAD(&wm.barq, (struct Object *)bar, entry);
	XRestackWindows(dpy, wins, 2);
	XMapWindow(dpy, win);
	barstrut(bar);
	monupdatearea();
}

/* delete bar */
int
unmanagebar(struct Object *obj, int dummy)
{
	struct Bar *bar;

	(void)dummy;
	bar = (struct Bar *)obj;
	TAILQ_REMOVE(&wm.barq, (struct Object *)bar, entry);
	free(bar);
	monupdatearea();
	return 0;
}

Class *bar_class = &(Class){
	.type           = TYPE_DOCK,
	.setstate       = NULL,
};
