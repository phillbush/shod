#include "shod.h"

#define TAB_FOREACH(container, tab) \
	for (struct Object *_col = TAILQ_FIRST(&(container)->colq); _col != NULL; _col = TAILQ_NEXT(_col, entry)) \
	for (struct Object *_row = TAILQ_FIRST(&((struct Column *)_col)->rowq); _row != NULL; _row = TAILQ_NEXT(_row, entry)) \
	for (\
		tab = (struct Tab *)TAILQ_FIRST(&((struct Row *)_row)->tabq);\
		tab != NULL;\
		tab = (struct Tab *)TAILQ_NEXT(&tab->obj, entry)\
	)

enum border {
	BORDER_S,
	BORDER_N,
	BORDER_W,
	BORDER_E,
	BORDER_SW,
	BORDER_SE,
	BORDER_NW,
	BORDER_NE,
	BORDER_LAST
};

struct Tab {
	struct Object obj;
	struct Queue dialq;
	struct Row *row;
	Window leader;
	Window title;
	Window close_btn;

	/*
	 * First we draw into pixmaps, and then copy their contents
	 * into the frame and title windows themselves whenever they
	 * are damaged.  It is necessary to redraw on the pixmaps only
	 * when the titlebar or frame resizes; so we save the geometry
	 * of hte pixmaps to compare with the size of their windows.
	 */
	Pixmap pix;                             /* pixmap to draw the background of the frame */
	Pixmap pixtitle;                        /* pixmap to draw the background of the title window */
	int ptw;                                /* pixmap width for the title window */
	int pw, ph;                             /* pixmap size of the frame */

	/*
	 * Geometry of the title bar (aka tab).
	 */
	int x, w;                               /* title bar geometry */
	unsigned int pid;

	/*
	 * Name of the tab's application window, its size and urgency.
	 */
	int winw, winh;                         /* window geometry */
	Bool isurgent;                          /* whether tab is urgent */
	char *name;                             /* client name */
};

struct Dialog {
	struct Object obj;
	struct Tab *tab;                        /* pointer to parent tab */

	/*
	 * Frames, pixmaps, saved pixmap geometry, etc
	 */
	Pixmap pix;                             /* pixmap to draw the frame */
	int pw, ph;                             /* pixmap size */

	/*
	 * Dialog geometry, which can be resized as the user resizes the
	 * container.  The dialog can grow up to a maximum width and
	 * height.
	 */
	int x, y, w, h;                         /* geometry of the dialog inside the tab frame */
	int maxw, maxh;                         /* maximum size of the dialog */

	int ignoreunmap;                        /* number of unmapnotifys to ignore */
};

struct Row {
	struct Object obj;

	struct Queue tabq;                      /* list of tabs */

	/*
	 * Each columnt is split vertically into rows; and each row
	 * contains tabs.  We maintain in a row its list of tabs, and
	 * a pointer to its parent column.
	 */
	struct Column *col;                     /* pointer to parent column */
	struct Tab *seltab;                     /* pointer to selected tab */
	int ntabs;                              /* number of tabs */

	/* At the bottom of each column, except the bottomost one, ther
	 * is a divisor handle which can be dragged to resize the row.
	 * There are also other windows forming a row:
	 * - The divisor.
	 * - The frame below the tab windows.
	 * - The title bar where the tabs are drawn.
	 * - The left (row maximize) button.
	 * - The right (close) button.
	 */
	Window tab_area;
	Window bar;                             /* title bar frame */

	/*
	 * We only keep the vertical geometry of a row (ie', its y
	 * position and its height), because, since a row horizontally
	 * spans its parent column width, its horizontal geometry is
	 * equal to the geometry of its parent column.
	 */
	double fact;                            /* factor of height relative to container */
	int y, h;                               /* row geometry */

	Bool isunmapped;
};

struct Column {
	struct Object obj;

	struct Queue rowq;                      /* list of rows */

	/*
	 * Each container is split horizontally into columns; and each
	 * column is split vertically into rows.  We maintain in a
	 * column its list of rows, and a pointer to its parent
	 * container.
	 */
	struct Container *c;                    /* pointer to parent container */
	struct Row *selrow;                     /* pointer to selected row */

	/*
	 * We only keep the horizontal geometry of a column (ie', its x
	 * position and its width), because, since a column vertically
	 * spans its parent container height, its vertical geometry is
	 * equal to the geometry of its parent container.
	 */
	double fact;                            /* factor of width relative to container */
	int nrows;                              /* number of rows */
	int x, w;                               /* column geometry */
};

struct Container {
	struct Object obj;

	/*
	 * A container contains a list of columns.
	 * A column contains a list of rows.
	 * A row contains a list of tabs.
	 * A tab contains an application window and a list of menus and
	 * a list of dialogs.
	 *
	 * A container with no column is a dummy container, used as
	 * placeholders on the Z-axis list.
	 */
	struct Object *layertop;
	struct Queue colq;                      /* list of columns in container */
	struct Column *selcol;                  /* pointer to selected container */
	int ncols;                              /* number of columns */

	/*
	 * A container appears on a certain desktop of a certain monitor.
	 */
	struct Monitor *mon;                    /* monitor container is on */
	int desk;                               /* desktop container is on */

	Window borders[BORDER_LAST];

	struct {
		XRectangle saved;
		XRectangle current;
	} geometry;

	/*
	 * Container state bitmask.
	 */
	enum State state;
	Bool ishidden;                          /* whether container is hidden */
};

static int nclients = 0;
static struct Queue focus_history;

static struct {
	Pixmap bar_vert;
	Pixmap bar_horz;
	Pixmap corner_nw;
	Pixmap corner_ne;
	Pixmap corner_sw;
	Pixmap corner_se;
} decorations[STYLE_LAST];

static int
partition(int n, int a, int b)
{
	return ((n + 1) * a / b) - (n * a / b);
}

static Bool
is_visible(struct Container *container, struct Monitor *mon, int desk)
{
	return container != NULL &&
		!(container->state & MINIMIZED) &&
		container->mon == mon &&
		(container->state & STICKY || container->desk == desk);
}

static Bool
is_focused(struct Container *container)
{
	if (wm.focused == NULL)
		return False;
	if (wm.focused->class != &tab_class)
		return False;
	return container == ((struct Tab *)wm.focused->self)->row->col->c;
}

static int
get_style(struct Container *container)
{
	return is_focused(container)?FOCUSED:UNFOCUSED;
}

static void
set_active_window(Window window)
{
	XChangeProperty(
		wm.display, wm.rootwin, wm.atoms[_NET_ACTIVE_WINDOW],
		XA_WINDOW, 32, PropModeReplace,
		(void *)&window, 1
	);
}

static void
set_window_desktop(Window window, int desk)
{
	XChangeProperty(
		wm.display, window,
		wm.atoms[_NET_WM_DESKTOP], XA_CARDINAL, 32,
		PropModeReplace, (void *)&desk, 1
	);
}

static void
set_container_group(struct Container *container)
{
	struct Tab *tab;

	TAB_FOREACH(container, tab) {
		XChangeProperty(
			wm.display, tab->obj.win,
			wm.atoms[_SHOD_GROUP_CONTAINER], XA_WINDOW, 32,
			PropModeReplace,
			(void *)&container->selcol->selrow->seltab->obj.win, 1
		);
	}
}

static void
setstate_recursive(struct Container *container)
{
	struct Tab *tab;
	struct Object *d;
	Atom data[9];
	int n = 0;
	int state = container->ishidden ? IconicState : NormalState;

	if (container->state & FULLSCREEN)
		data[n++] = wm.atoms[_NET_WM_STATE_FULLSCREEN];
	if (container->state & STICKY)
		data[n++] = wm.atoms[_NET_WM_STATE_STICKY];
	if (container->state & SHADED)
		data[n++] = wm.atoms[_NET_WM_STATE_SHADED];
	if (container->state & MINIMIZED)
		data[n++] = wm.atoms[_NET_WM_STATE_HIDDEN];
	if (container->state & ABOVE)
		data[n++] = wm.atoms[_NET_WM_STATE_ABOVE];
	if (container->state & BELOW)
		data[n++] = wm.atoms[_NET_WM_STATE_BELOW];
	if (container->state & MAXIMIZED) {
		data[n++] = wm.atoms[_NET_WM_STATE_MAXIMIZED_VERT];
		data[n++] = wm.atoms[_NET_WM_STATE_MAXIMIZED_HORZ];
	}
	TAB_FOREACH(container, tab) {
		int nstates = n;

		if (&tab->obj == wm.focused)
			data[nstates++] = wm.atoms[_NET_WM_STATE_FOCUSED];
		XChangeProperty(
			wm.display, tab->obj.win, wm.atoms[_NET_WM_STATE],
			XA_ATOM, 32, PropModeReplace,
			(void *)data, nstates
		);
		XChangeProperty(
			wm.display, tab->obj.win, wm.atoms[WM_STATE], wm.atoms[WM_STATE],
			32, PropModeReplace, (void *)&(long[]){
				[0] = state,
				[1] = None,
			}, 2
		);
		TAILQ_FOREACH(d, &tab->dialq, entry) {
			XChangeProperty(
				wm.display, d->win, wm.atoms[_NET_WM_STATE],
				XA_ATOM, 32, PropModeReplace,
				(void *)data, nstates
			);
			XChangeProperty(
				wm.display, d->win, wm.atoms[WM_STATE], wm.atoms[WM_STATE],
				32, PropModeReplace, (void *)&(long[]){
					[0] = state,
					[1] = None,
				}, 2
			);
		}
	}
}

static void
containerhide(struct Container *c, int hide)
{
	if (c == NULL)
		return;
	c->ishidden = hide;
	if (hide)
		XUnmapWindow(wm.display, c->obj.frame);
	else
		XMapWindow(wm.display, c->obj.frame);
}

void
container_setdesk(struct Container *container)
{
	struct Tab *tab;
	unsigned long desk;

	if (container->state & (STICKY|MINIMIZED))
		desk = 0xFFFFFFFF;
	else
		desk = (unsigned long)container->desk;
	TAB_FOREACH(container, tab) {
		set_window_desktop(tab->obj.win, desk);
	}
}

/* increment number of clients */
static void
clientsincr(void)
{
	nclients++;
}

/* decrement number of clients */
static void
clientsdecr(void)
{
	nclients--;
}

static void
tabdecorate(struct Tab *t, int style)
{
	int drawlines = 0;

	if (t->isurgent)
		style = URGENT;
	if (t->row != NULL && t != t->row->col->c->selcol->selrow->seltab)
		drawlines = 0;
	else
		drawlines = 1;

	updatepixmap(&t->pixtitle, &t->ptw, NULL, t->w, config.titlewidth);
	updatepixmap(&t->pix, &t->pw, &t->ph, t->winw, t->winh);
	drawbackground(t->pixtitle, 0, 0, t->w, config.titlewidth, style);
	drawshadow(t->pixtitle, 0, 0, t->w, config.titlewidth, style);

	/* write tab title */
	if (t->name != NULL)
		drawtitle(t->pixtitle, t->name, t->w, drawlines, style, False, 0);

	/* draw frame background */
	drawbackground(t->pix, 0, 0, t->winw, t->winh, style);

	drawcommit(t->pixtitle, t->title);
	drawcommit(t->pix, t->obj.frame);
	drawcommit(
		wm.close_btn[style][t == t->row->col->c->selcol->selrow->seltab],
		t->close_btn
	);
}

static void
dialogdecorate(struct Dialog *d)
{
	int fullw, fullh;       /* size of dialog window + borders */

	fullw = d->w + 2 * config.borderwidth;
	fullh = d->h + 2 * config.borderwidth;
	updatepixmap(&d->pix, &d->pw, &d->ph, fullw, fullh);
	drawborders(d->pix, fullw, fullh, FOCUSED);
	drawcommit(d->pix, d->obj.frame);
}

static void
container_decorate(struct Container *container, int style)
{
	struct Object *l, *r, *t, *d;

	backgroundcommit(container->obj.frame, style);
	drawcommit(decorations[style].bar_horz, container->borders[BORDER_N]);
	drawcommit(decorations[style].bar_horz, container->borders[BORDER_S]);
	drawcommit(decorations[style].bar_vert, container->borders[BORDER_W]);
	drawcommit(decorations[style].bar_vert, container->borders[BORDER_E]);
	drawcommit(decorations[style].corner_nw, container->borders[BORDER_NW]);
	drawcommit(decorations[style].corner_ne, container->borders[BORDER_NE]);
	drawcommit(decorations[style].corner_sw, container->borders[BORDER_SW]);
	drawcommit(decorations[style].corner_se, container->borders[BORDER_SE]);
	TAILQ_FOREACH(l, &container->colq, entry) {
		struct Column *col = l->self;
		drawcommit(decorations[style].bar_vert, col->obj.win);
		TAILQ_FOREACH(r, &col->rowq, entry) {
			struct Row *row = r->self;
			drawcommit(decorations[style].bar_horz, row->obj.win);
			backgroundcommit(row->bar, style);
			TAILQ_FOREACH(t, &row->tabq, entry) {
				tabdecorate(t->self, style);
				TAILQ_FOREACH(d, &((struct Tab *)t)->dialq, entry) {
					dialogdecorate(d->self);
				}
			}
		}
	}
}

static void
redecorate(struct Object *obj)
{
	struct Container *container;

	if (obj->class == &tab_class)
		container = ((struct Tab *)obj)->row->col->c;
	else if (obj->class == &container_class)
		container = obj->self;
	else
		return;
	container_decorate(container, get_style(container));
}

/* clear window urgency */
static void
tabclearurgency(struct Tab *tab)
{
	XWMHints wmh = {0};

	XSetWMHints(wm.display, tab->obj.win, &wmh);
	tab->isurgent = 0;
}

static void
tabupdateurgency(struct Tab *t, int isurgent)
{
	int prev;

	prev = t->isurgent;
	if (&t->obj == wm.focused)
		t->isurgent = False;
	else
		t->isurgent = isurgent;
	if (prev == t->isurgent)
		return;
	redecorate(&t->row->col->c->obj);
}

static void
dialogcalcsize(struct Dialog *dial)
{
	struct Tab *tab;

	tab = dial->tab;
	dial->w = max(1, min(dial->maxw, tab->winw - 2 * config.borderwidth));
	dial->h = max(1, min(dial->maxh, tab->winh - 2 * config.borderwidth));
	dial->x = tab->winw / 2 - dial->w / 2;
	dial->y = tab->winh / 2 - dial->h / 2;
}

static void
dialog_update_geometry(struct Dialog *dial)
{
	struct Container *c;
	int dx, dy, dw, dh;

	dialogcalcsize(dial);
	c = dial->tab->row->col->c;
	dx = dial->x - config.borderwidth;
	dy = dial->y - config.borderwidth;
	dw = dial->w + 2 * config.borderwidth;
	dh = dial->h + 2 * config.borderwidth;
	XMoveResizeWindow(wm.display, dial->obj.frame, dx, dy, dw, dh);
	XMoveResizeWindow(wm.display, dial->obj.win, config.borderwidth, config.borderwidth, dial->w, dial->h);
	window_configure_notify(
		wm.display, dial->obj.win,
		c->geometry.current.x + dial->tab->row->col->x + dial->x,
		c->geometry.current.y + dial->tab->row->y + dial->y,
		dial->w,
		dial->h
	);
	if (dial->pw != dw || dial->ph != dh) {
		dialogdecorate(dial);
	}
}

static void
tab_update_geometry(struct Tab *tab)
{
	struct Container *container = tab->row->col->c;
	struct Object *obj;

	TAILQ_FOREACH(obj, &tab->dialq, entry) {
		struct Dialog *dial = obj->self;
		dialog_update_geometry(dial);
	}
	XResizeWindow(wm.display, tab->obj.frame, tab->winw, tab->winh);
	XResizeWindow(wm.display, tab->obj.win, tab->winw, tab->winh);
	window_configure_notify(
		wm.display, tab->obj.win,
		container->geometry.current.x + tab->row->col->x,
		container->geometry.current.y + tab->row->y + config.titlewidth,
		tab->winw, tab->winh
	);
}

static int
containercontentwidth(struct Container *c)
{
	return c->geometry.current.width - (c->ncols - 1) * config.divwidth - 2 * config.borderwidth;
}

/* calculate position and width of tabs of a row */
static void
rowcalctabs(struct Row *row)
{
	struct Object *p, *d;
	int ntabs, x;

	if (TAILQ_EMPTY(&row->tabq))
		return;
	x = 0;
	ntabs = 0;
	TAILQ_FOREACH(p, &row->tabq, entry) {
		struct Tab *tab = p->self;

		tab->winh = max(1, row->h - config.titlewidth);
		tab->winw = row->col->w;
		tab->w = max(1, partition(ntabs, tab->winw, row->ntabs));
		tab->x = x;
		x += tab->w;
		ntabs++;
		TAILQ_FOREACH(d, &tab->dialq, entry)
			dialogcalcsize(d->self);
	}
}

static int
columncontentheight(struct Column *col)
{
	return col->c->geometry.current.height - col->nrows * config.titlewidth
	       - (col->nrows - 1) * config.divwidth - 2 * config.borderwidth;
}

/* calculate position and height of rows of a column */
static void
colcalcrows(struct Column *col, int recalcfact)
{
	struct Object *obj;
	int i, y, h, sumh;
	int content;
	int recalc;

	if (TAILQ_EMPTY(&col->rowq))
		return;
	if (col->c->state & FULLSCREEN) {
		TAILQ_FOREACH(obj, &col->rowq, entry) {
			struct Row *row = obj->self;
			row->y = -config.titlewidth;
			row->h = col->c->geometry.current.height + config.titlewidth;
			rowcalctabs(row);
		}
		return;
	}

	/* check if rows sum up the height of the container */
	content = columncontentheight(col);
	sumh = 0;
	recalc = 0;
	TAILQ_FOREACH(obj, &col->rowq, entry) {
		struct Row *row = obj->self;
		if (!recalcfact) {
			if (TAILQ_NEXT(obj, entry) == NULL) {
				row->h = content - sumh + config.titlewidth;
			} else {
				row->h = row->fact * content + config.titlewidth;
			}
			if (row->h < config.titlewidth) {
				recalc = 1;
			}
		}
		sumh += row->h - config.titlewidth;
	}
	if (sumh != content)
		recalc = 1;

	h = col->c->geometry.current.height - 2 * config.borderwidth - (col->nrows - 1) * config.divwidth;
	y = config.borderwidth;
	i = 0;
	TAILQ_FOREACH(obj, &col->rowq, entry) {
		struct Row *row = obj->self;
		if (recalc)
			row->h = max(config.titlewidth, partition(i, h, col->nrows));
		if (recalc || recalcfact)
			row->fact = (double)(row->h - config.titlewidth) / (double)(content);
		row->y = y;
		y += row->h + config.divwidth;
		rowcalctabs(row);
		i++;
	}
}

/* calculate position and width of columns of a container */
static void
containercalccols(struct Container *c)
{
	struct Object *obj;
	int i, x, w;
	int sumw;
	int content;
	int recalc;

	if (c->state & FULLSCREEN) {
		TAILQ_FOREACH(obj, &c->colq, entry) {
			struct Column *col = obj->self;
			col->x = 0;
			col->w = c->geometry.current.width;
			colcalcrows(col, 0);
		}
		return;
	}

	/* check if columns sum up the width of the container */
	content = containercontentwidth(c);
	sumw = 0;
	recalc = 0;
	TAILQ_FOREACH(obj, &c->colq, entry) {
		struct Column *col = obj->self;
		if (TAILQ_NEXT(obj, entry) == NULL)
			col->w = content - sumw;
		else
			col->w = col->fact * content;
		if (col->w == 0)
			recalc = 1;
		sumw += col->w;
	}
	if (sumw != content)
		recalc = 1;

	w = c->geometry.current.width - 2 * config.borderwidth - (c->ncols - 1) * config.divwidth;
	x = config.borderwidth;
	i = 0;
	TAILQ_FOREACH(obj, &c->colq, entry) {
		struct Column *col = obj->self;
		if (c->state & SHADED)
			c->geometry.current.height = max(c->geometry.current.height, col->nrows * config.titlewidth);
		if (recalc)
			col->w = max(1, partition(i, w, c->ncols));
		if (recalc)
			col->fact = (double)col->w/(double)c->geometry.current.width;
		col->x = x;
		x += col->w + config.divwidth;
		colcalcrows(col, 0);
		i++;
	}
	if (c->state & SHADED) {
		c->geometry.current.height += 2 * config.borderwidth;
	}
}

static void
update_tiles(struct Container *c)
{
	struct Object *l, *r, *t;
	int rowy;

	containercalccols(c);
	TAILQ_FOREACH(l, &c->colq, entry) {
		struct Column *col = l->self;
		rowy = config.borderwidth;
		if (TAILQ_NEXT(l, entry) != NULL) {
			XMoveResizeWindow(
				wm.display, col->obj.win,
				col->x + col->w, config.borderwidth,
				config.divwidth,
				c->geometry.current.height - 2 * config.borderwidth
			);
			XMapWindow(wm.display, col->obj.win);
		} else {
			XUnmapWindow(wm.display, col->obj.win);
		}
		TAILQ_FOREACH(r, &col->rowq, entry) {
			struct Row *row = r->self;
			if ((c->state & SHADED) && !(c->state & FULLSCREEN)) {
				XMoveResizeWindow(
					wm.display, row->bar,
					col->x, rowy, col->w,
					config.titlewidth
				);
				XUnmapWindow(wm.display, row->tab_area);
				XUnmapWindow(wm.display, row->obj.win);
				row->isunmapped = True;
			} else {
				if (TAILQ_NEXT(r, entry) != NULL) {
					XMoveResizeWindow(wm.display, row->obj.win, col->x, row->y + row->h, col->w, config.divwidth);
					XMapWindow(wm.display, row->obj.win);
				} else {
					XUnmapWindow(wm.display, row->obj.win);
				}
				XMoveResizeWindow(
					wm.display, row->bar,
					col->x, row->y, col->w,
					config.titlewidth
				);
				if (row->h - config.titlewidth > 0) {
					XMoveResizeWindow(wm.display, row->tab_area, col->x, row->y + config.titlewidth, col->w, row->h - config.titlewidth);
					XMapWindow(wm.display, row->tab_area);
					row->isunmapped = False;
				} else {
					XUnmapWindow(wm.display, row->tab_area);
					row->isunmapped = True;
				}
			}
			rowy += config.titlewidth;
			TAILQ_FOREACH(t, &row->tabq, entry) {
				struct Tab *tab = t->self;

				XMoveResizeWindow(
					wm.display, tab->title,
					tab->x, 0,
					tab->w, config.titlewidth
				);
				if (tab->ptw != tab->w)
					tabdecorate(tab, get_style(c));
				if (tab == row->seltab) {
					tab_update_geometry(tab);
				}
			}
		}
	}
}

static void
containermoveresize(struct Container *container, XRectangle geometry)
{
	int dy;

	if ((container->state & SHADED) && !(container->state & FULLSCREEN)) {
		struct Object *obj;
		geometry.height = 0;
		TAILQ_FOREACH(obj, &container->colq, entry) {
			struct Column *col = obj->self;
			geometry.height = max(
				geometry.height,
				col->nrows * config.titlewidth
			);
		}
		geometry.height += 2 * config.borderwidth;
	}
	XMoveResizeWindow(
		wm.display, container->obj.frame,
		geometry.x, geometry.y,
		geometry.width, geometry.height
	);
	XResizeWindow(
		wm.display, container->borders[BORDER_N],
		geometry.width, config.borderwidth
	);
	XResizeWindow(
		wm.display, container->borders[BORDER_S],
		geometry.width, config.borderwidth
	);
	dy = 2 * (config.borderwidth + 1);
	XResizeWindow(
		wm.display, container->borders[BORDER_W],
		config.borderwidth, geometry.height - dy
	);
	XResizeWindow(
		wm.display, container->borders[BORDER_E],
		config.borderwidth, geometry.height - dy
	);
	container->geometry.current = geometry;
	if (!(container->state & STICKY)) {
		struct Monitor *monto = getmon(
			geometry.x + geometry.width / 2,
			geometry.y + geometry.height / 2
		);

		if (monto != NULL && monto != container->mon) {
			container->mon = monto;
			container->desk = monto->seldesk;
			container_setdesk(container);
			if (is_focused(container))
				deskupdate(monto, monto->seldesk);
		}
	}
	update_tiles(container);
}

static void
container_update_geometry(struct Container *container)
{
	if (container == NULL || container->state & MINIMIZED)
		return;
	if (container->state & FULLSCREEN) {
		container->geometry.current = container->mon->geometry;
	} else if (container->state & MAXIMIZED) {
		container->geometry.current = container->mon->window_area;
	} else {
		container->geometry.current = container->geometry.saved;
		container->geometry.saved.x = container->geometry.current.x;
		container->geometry.saved.y = container->geometry.current.y;
		container->geometry.saved.width = container->geometry.current.width;
		if (!(container->state & SHADED)) {
			container->geometry.saved.height = container->geometry.current.height;
		}
	}
	containermoveresize(container, container->geometry.current);
}

static void
containerinsertfocus(struct Container *container)
{
	TAILQ_INSERT_HEAD(&focus_history, &container->obj, entry);
}

static void
containerdelfocus(struct Container *container)
{
	TAILQ_REMOVE(&focus_history, &container->obj, entry);
}

static void
containerdelraise(struct Container *container)
{
	TAILQ_REMOVE(&wm.stacking_order, &container->obj, z_entry);
}

static void
containeraddfocus(struct Container *c)
{
	if (c == NULL || c->state & MINIMIZED)
		return;
	containerdelfocus(c);
	containerinsertfocus(c);
}

static void
rowstretch(struct Column *col, struct Row *row)
{
	struct Object *r;
	double fact;
	int refact;

	fact = 1.0 / (double)col->nrows;
	refact = (row->fact == 1.0);
	TAILQ_FOREACH(r, &col->rowq, entry) {
		if (refact) {
			((struct Row *)r)->fact = fact;
		} else if (((struct Row *)r) == row) {
			((struct Row *)r)->fact = 1.0;
		} else {
			((struct Row *)r)->fact = 0.0;
		}
	}
	colcalcrows(col, 0);
	update_tiles(col->c);
}

static void
dialog_focus(struct Dialog *dial)
{
	XRaiseWindow(wm.display, dial->obj.frame);
	XSetInputFocus(wm.display, dial->obj.win, RevertToPointerRoot, CurrentTime);
}

static void
restack(struct Object *obj)
{
	struct Container *container;
	struct Object *above;

	if (obj->class == &tab_class)
		container = ((struct Tab *)obj)->row->col->c;
	else if (obj->class == &container_class)
		container = obj->self;
	else
		return;
	above = TAILQ_PREV(&container->obj, Queue, z_entry);
	XRestackWindows(wm.display, (Window[]){
		above->frame,
		container->obj.frame,
	}, 2);
}

static void
raise(struct Container *container)
{
	containerdelraise(container);
	if (is_focused(container) && (container->state & FULLSCREEN))
		container->layertop = &wm.layers[LAYER_FULLSCREEN];
	else if (container->state & ABOVE)
		container->layertop = &wm.layers[LAYER_ABOVE];
	else if (container->state & BELOW)
		container->layertop = &wm.layers[LAYER_BELOW];
	else
		container->layertop = &wm.layers[LAYER_NORMAL];
	TAILQ_INSERT_AFTER(
		&wm.stacking_order,
		container->layertop, &container->obj,
		z_entry
	);
	restack(&container->obj);
}

static void
tabfocus(struct Tab *tab)
{
	struct Container *container;
	struct Object *prevfocused = wm.focused;

	if (tab == NULL) {
		if (wm.focused == NULL)
			return;
		wm.focused = NULL;
		XSetInputFocus(wm.display, wm.focuswin, RevertToPointerRoot, CurrentTime);
		set_active_window(None);
	} else {
		container = tab->row->col->c;
		wm.focused = &tab->obj;
		tab->row->seltab = tab;
		tab->row->col->selrow = tab->row;
		tab->row->col->c->selcol = tab->row->col;
		tab_update_geometry(tab);
		deskshow(False);
		if (tab->row->fact == 0.0)
			rowstretch(tab->row->col, tab->row);
		XRaiseWindow(wm.display, tab->row->tab_area);
		XRaiseWindow(wm.display, tab->obj.frame);
		if (container->state & SHADED || tab->row->isunmapped) {
			XSetInputFocus(wm.display, tab->row->bar, RevertToPointerRoot, CurrentTime);
		} else if (!TAILQ_EMPTY(&tab->dialq)) {
			dialog_focus(TAILQ_FIRST(&tab->dialq)->self);
		} else {
			XSetInputFocus(wm.display, tab->obj.win, RevertToPointerRoot, CurrentTime);
		}
		set_active_window(tab->obj.win);
		tabclearurgency(tab);
		containeraddfocus(container);
		container_decorate(container, FOCUSED);
		container->state &= ~MINIMIZED;
		containerhide(container, 0);
		set_container_group(container);
		setstate_recursive(container);
		menu_class.restack_all();
		if (is_visible(container, wm.selmon, wm.selmon->seldesk)) {
			deskupdate(
				container->mon,
				container->state & STICKY ?
				container->mon->seldesk : container->desk
			);
		}
	}
	if (prevfocused != NULL && (tab == NULL || tab != prevfocused->self)) {
		if (prevfocused->class->redecorate != NULL)
			prevfocused->class->redecorate(prevfocused);
		if (prevfocused->class == &tab_class) {
			struct Container *c;

			c = ((struct Tab *)prevfocused->self)->row->col->c;
			setstate_recursive(c);
			if (c->state & FULLSCREEN)
				restack(prevfocused);
		}
	}
	if (wm.showingdesk)
		menu_class.show_desktop();
	wm.setclientlist = True;
}

static void
dialog_configure(struct Object *self, unsigned int valuemask, XWindowChanges *wc)
{
	struct Dialog *dialog = self->self;

	if (dialog == NULL)
		return;
	if (valuemask & CWWidth)
		dialog->maxw = wc->width;
	if (valuemask & CWHeight)
		dialog->maxh = wc->height;
	dialog_update_geometry(dialog);
}

static void
managedialog(struct Object *app, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Tab *tab = app->self;
	struct Dialog *dial;

	(void)mon;
	(void)desk;
	(void)leader;
	(void)state;
	dial = emalloc(sizeof(*dial));
	*dial = (struct Dialog){
		.pix = None,
		.maxw = rect.width,
		.maxh = rect.height,
		.obj.win = win,
		.obj.self = dial,
		.obj.class = &dialog_class,
		.obj.frame = createframe(rect),
	};
	context_add(win, &dial->obj);
	context_add(dial->obj.frame, &dial->obj);
	XReparentWindow(wm.display, dial->obj.win, dial->obj.frame, 0, 0);
	XChangeProperty(
		wm.display, win, wm.atoms[_NET_FRAME_EXTENTS],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)(long[]){
			config.borderwidth,
			config.borderwidth,
			config.borderwidth,
			config.borderwidth,
		}, 4
	);
	XMapWindow(wm.display, dial->obj.win);
	dial->tab = tab;
	TAILQ_INSERT_HEAD(&tab->dialq, (struct Object *)dial, entry);
	XReparentWindow(wm.display, dial->obj.frame, tab->obj.frame, 0, 0);
	dialog_update_geometry(dial);
	XMapRaised(wm.display, dial->obj.frame);
	if (&tab->obj == wm.focused)
		dialog_focus(dial);
}

static void
unmanagedialog(struct Object *obj)
{
	struct Dialog *dial = obj->self;

	TAILQ_REMOVE(&dial->tab->dialq, (struct Object *)dial, entry);
	if (dial->pix != None)
		XFreePixmap(wm.display, dial->pix);
	XReparentWindow(wm.display, dial->obj.win, wm.rootwin, 0, 0);
	context_del(dial->obj.win);
	XDestroyWindow(wm.display, dial->obj.frame);
	free(dial);
	wm.setclientlist = True;
}

static struct Tab *
tabnew(Window win, Window leader)
{
	struct Tab *tab;

	tab = emalloc(sizeof(*tab));
	*tab = (struct Tab){
		.pix = None,
		.pixtitle = None,
		.title = None,
		.leader = leader,
		.obj.win = win,
		.obj.self = tab,
		.obj.class = &tab_class,
		.obj.frame = createframe((XRectangle){0, 0, 1, 1}),
	};
	TAILQ_INIT(&tab->dialq);
	tab->title = createdecoration(
		wm.rootwin, (XRectangle){0, 0, 1, config.titlewidth},
		None, NorthWestGravity
	);
	tab->close_btn = createdecoration(
		tab->title, (XRectangle){
			1 - config.titlewidth, 0,
			config.titlewidth, config.titlewidth
		},
		wm.cursors[CURSOR_PIRATE], NorthEastGravity
	);
	context_add(tab->obj.win, &tab->obj);
	context_add(tab->obj.frame, &tab->obj);
	context_add(tab->title, &tab->obj);
	context_add(tab->close_btn, &tab->obj);
	tab->pid = getcardprop(wm.display, tab->obj.win, wm.atoms[_NET_WM_PID]);
	XReparentWindow(wm.display, tab->obj.win, tab->obj.frame, 0, 0);
	XChangeProperty(
		wm.display, win, wm.atoms[_NET_FRAME_EXTENTS],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)(long[]){
			config.borderwidth,
			config.borderwidth,
			config.borderwidth + config.titlewidth,
			config.borderwidth,
		}, 4
	);
	XMapWindow(wm.display, tab->obj.win);
	XMapWindow(wm.display, tab->close_btn);
	clientsincr();
	return tab;
}

/* remove tab from row's tab queue */
static void
tabremove(struct Row *row, struct Tab *tab)
{
	if (row->seltab == tab) {
		row->seltab = (void *)TAILQ_NEXT((struct Object *)tab, entry);
		if (row->seltab == NULL) {
			row->seltab = (void *)TAILQ_PREV((struct Object *)tab, Queue, entry);
		}
	}
	row->ntabs--;
	TAILQ_REMOVE(&row->tabq, (struct Object *)tab, entry);
	tab->row = NULL;
}

static void
tabdel(struct Tab *tab)
{
	struct Object *dial;

	while ((dial = TAILQ_FIRST(&tab->dialq)) != NULL) {
		XDestroyWindow(wm.display, dial->win);
		unmanagedialog((struct Object *)dial);
	}
	tabremove(tab->row, tab);
	if (tab->pixtitle != None)
		XFreePixmap(wm.display, tab->pixtitle);
	if (tab->pix != None)
		XFreePixmap(wm.display, tab->pix);
	if (&tab->obj == wm.focused)
		wm.focused = NULL;
	XReparentWindow(wm.display, tab->obj.win, wm.rootwin, 0, 0);
	context_del(tab->obj.win);
	XDestroyWindow(wm.display, tab->title);
	XDestroyWindow(wm.display, tab->close_btn);
	XDestroyWindow(wm.display, tab->obj.frame);
	clientsdecr();
	free(tab->name);
	free(tab);
}

static struct Row *
rownew(void)
{
	extern struct Class row_class;
	struct Row *row;

	row = emalloc(sizeof(*row));
	*row = (struct Row){
		.isunmapped = False,
		.obj.win = createdecoration(
			wm.rootwin,
			(XRectangle){0, 0, 1, 1},
			wm.cursors[CURSOR_V], WestGravity
		),
		.obj.self = row,
		.obj.class = &row_class,
	};
	TAILQ_INIT(&row->tabq);
	row->tab_area = createwindow(wm.rootwin, (XRectangle){0, 0, 1, 1}, 0, NULL);
	row->bar = createdecoration(
		wm.rootwin,
		(XRectangle){0, 0, config.titlewidth, config.titlewidth},
		None, NorthGravity
	);
	context_add(row->obj.win, &row->obj);
	return row;
}

/* detach row from column */
static void
rowdetach(struct Row *row, int recalc)
{
	struct Column *col;

	col = row->col;
	if (col->selrow == row) {
		col->selrow = (void *)TAILQ_PREV(&row->obj, Queue, entry);
		if (col->selrow == NULL) {
			col->selrow = (void *)TAILQ_NEXT(&row->obj, entry);
		}
	}
	col->nrows--;
	TAILQ_REMOVE(&col->rowq, &row->obj, entry);
	if (recalc) {
		colcalcrows(row->col, 1);
	}
}

/* delete row */
static void
rowdel(struct Row *row)
{
	struct Object *tab;

	while ((tab = TAILQ_FIRST(&row->tabq)) != NULL)
		tabdel(tab->self);
	rowdetach(row, 1);
	XDestroyWindow(wm.display, row->tab_area);
	XDestroyWindow(wm.display, row->bar);
	XDestroyWindow(wm.display, row->obj.win);
	free(row);
}

/* create new column */
static struct Column *
colnew(void)
{
	extern struct Class column_class;
	struct Column *col;

	col = emalloc(sizeof(*col));
	*col = (struct Column){
		.obj.win = createdecoration(
			wm.rootwin,
			(XRectangle){0, 0, 1, 1},
			wm.cursors[CURSOR_H], NorthGravity
		),
		.obj.self = col,
		.obj.class = &column_class,
	};
	TAILQ_INIT(&col->rowq);
	context_add(col->obj.win, &col->obj);
	return col;
}

#define DIV 15  /* see containerplace() for details */

/* fill placement grid for given rectangle */
static void
fillgrid(struct Monitor *mon, int x, int y, int w, int h, int grid[DIV][DIV])
{
	int i, j;
	int ha, hb, wa, wb;
	int ya, yb, xa, xb;

	for (i = 0; i < DIV; i++) {
		for (j = 0; j < DIV; j++) {
			ha = mon->window_area.y + (mon->window_area.height * i)/DIV;
			hb = mon->window_area.y + (mon->window_area.height * (i + 1))/DIV;
			wa = mon->window_area.x + (mon->window_area.width * j)/DIV;
			wb = mon->window_area.x + (mon->window_area.width * (j + 1))/DIV;
			ya = y;
			yb = y + h;
			xa = x;
			xb = x + w;
			if (ya <= hb && ha <= yb && xa <= wb && wa <= xb) {
				if (ya < ha && yb > hb)
					grid[i][j]++;
				if (xa < wa && xb > wb)
					grid[i][j]++;
				grid[i][j]++;
			}
		}
	}
}

/* find best position to place a container on wm.screen */
static void
containerplace(struct Container *c, struct Monitor *mon, int desk, int userplaced)
{
	struct Object *obj;
	int grid[DIV][DIV] = {{0}, {0}};
	int lowest;
	int i, j, k, w, h;
	int subx, suby;         /* position of the larger subregion */
	int subw, subh;         /* larger subregion width and height */

	if (desk < 0 || desk >= config.ndesktops)
		return;
	if (c == NULL || c->state & MINIMIZED)
		return;

	fitmonitor(mon, &c->geometry.saved, 1.0);

	/* if the user placed the window, we should not re-place it */
	if (userplaced)
		return;

	/*
	 * The container area is the region of the wm.screen where containers live,
	 * that is, the area of the monitor not occupied by bars or the dock; it
	 * corresponds to the region occupied by a maximized container.
	 *
	 * Shod tries to find an empty region on the container area or a region
	 * with few containers in it to place a new container.  To do that, shod
	 * cuts the container area in DIV divisions horizontally and vertically,
	 * creating DIV*DIV regions; shod then counts how many containers are on
	 * each region; and places the new container on those regions with few
	 * containers over them.
	 *
	 * After some trial and error, I found out that a DIV equals to 15 is
	 * optimal.  It is not too low to provide a incorrect placement, nor too
	 * high to take so much computer time.
	 */

	/* increment cells of grid a window is in */
	TAILQ_FOREACH(obj, &focus_history, entry) {
		struct Container *tmp = obj->self;
		if (tmp != c && is_visible(tmp, c->mon, c->desk)) {
			fillgrid(
				mon,
				tmp->geometry.saved.x,
				tmp->geometry.saved.y,
				tmp->geometry.saved.width,
				tmp->geometry.saved.height,
				grid
			);
		}
	}

	/* find biggest region in grid with less windows in it */
	lowest = INT_MAX;
	subx = suby = 0;
	subw = subh = 0;
	for (i = 0; i < DIV; i++) {
		for (j = 0; j < DIV; j++) {
			if (grid[i][j] > lowest)
				continue;
			else if (grid[i][j] < lowest) {
				lowest = grid[i][j];
				subw = subh = 0;
			}
			for (w = 0; j+w < DIV && grid[i][j + w] == lowest; w++)
				;
			for (h = 1; i+h < DIV && grid[i + h][j] == lowest; h++) {
				for (k = 0; k < w && grid[i + h][j + k] == lowest; k++)
					;
				if (k < w) {
					h--;
					break;
				}
			}
			if (w * h > subw * subh) {
				subw = w;
				subh = h;
				suby = i;
				subx = j;
			}
		}
	}
	subx = subx * mon->window_area.width / DIV;
	suby = suby * mon->window_area.height / DIV;
	subw = subw * mon->window_area.width / DIV;
	subh = subh * mon->window_area.height / DIV;
	c->geometry.saved.x = min(
		mon->window_area.x + mon->window_area.width - c->geometry.saved.width,
		max(
			mon->window_area.x,
			mon->window_area.x + subx + subw / 2 - c->geometry.saved.width / 2
		)
	);
	c->geometry.saved.y = min(
		mon->window_area.y + mon->window_area.height - c->geometry.saved.height,
		max(
			mon->window_area.y,
			mon->window_area.y + suby + subh / 2 - c->geometry.saved.height / 2
		)
	);
	containercalccols(c);
}

/* detach column from container */
static void
coldetach(struct Column *col)
{
	struct Container *c;

	c = col->c;
	if (c->selcol == col) {
		c->selcol = (void *)TAILQ_PREV(&col->obj, Queue, entry);
		if (c->selcol == NULL) {
			c->selcol = (void *)TAILQ_NEXT(&col->obj, entry);
		}
	}
	c->ncols--;
	TAILQ_REMOVE(&c->colq, &col->obj, entry);
	containercalccols(col->c);
}

static void
coldel(struct Column *col)
{
	struct Object *row;

	while ((row = TAILQ_FIRST(&col->rowq)) != NULL)
		rowdel(row->self);
	coldetach(col);
	XDestroyWindow(wm.display, col->obj.win);
	free(col);
}

static void
coladdrow(struct Column *col, struct Row *row, struct Row *prev)
{
	struct Container *c;
	struct Column *oldcol;

	c = col->c;
	oldcol = row->col;
	row->col = col;
	col->selrow = row;
	col->nrows++;
	if (prev == NULL || TAILQ_EMPTY(&col->rowq))
		TAILQ_INSERT_HEAD(&col->rowq, &row->obj, entry);
	else
		TAILQ_INSERT_AFTER(&col->rowq, &prev->obj, &row->obj, entry);
	colcalcrows(col, 1);    /* set row->y, row->h, etc */
	XReparentWindow(wm.display, row->obj.win, c->obj.frame, col->x + col->w, config.borderwidth);
	XReparentWindow(wm.display, row->bar, c->obj.frame, col->x, row->y);
	XReparentWindow(wm.display, row->tab_area, c->obj.frame, col->x, row->y);
	XMapWindow(wm.display, row->bar);
	XMapWindow(wm.display, row->tab_area);
	if (oldcol != NULL && oldcol->nrows == 0) {
		coldel(oldcol);
	}
}

static void
rowaddtab(struct Row *row, struct Tab *tab, struct Tab *prev)
{
	struct Row *oldrow;

	oldrow = tab->row;
	tab->row = row;
	row->ntabs++;
	if (prev == NULL || TAILQ_EMPTY(&row->tabq))
		TAILQ_INSERT_HEAD(&row->tabq, (struct Object *)tab, entry);
	else
		TAILQ_INSERT_AFTER(&row->tabq, (struct Object *)prev, (struct Object *)tab, entry);
	rowcalctabs(row);               /* set tab->x, tab->w, etc */
	XReparentWindow(wm.display, tab->title, row->bar, tab->x, 0);
	XReparentWindow(wm.display, tab->obj.frame, row->tab_area, 0, 0);
	XMapWindow(wm.display, tab->obj.frame);
	XMapWindow(wm.display, tab->title);
	if (oldrow != NULL) {           /* deal with the row this tab came from */
		if (oldrow->ntabs == 0) {
			rowdel(oldrow);
		} else {
			rowcalctabs(oldrow);
		}
	}
}

static void containerstick(struct Container *c, int stick);

/* send container to desktop and raise it; return nonzero if it was actually sent anywhere */
static int
containersendtodesk(struct Container *c, struct Monitor *mon, unsigned long desk)
{
	if (c == NULL || c->state & MINIMIZED)
		return 0;
	if (desk == 0xFFFFFFFF) {
		containerstick(c, ADD);
	} else if ((int)desk < config.ndesktops) {
		c->desk = (int)desk;
		if (c->mon != mon)
			containerplace(c, mon, desk, 1);
		c->mon = mon;
		c->state &= ~STICKY;
		if ((int)desk != mon->seldesk)  /* container was sent to invisible desktop */
			containerhide(c, 1);
		else
			containerhide(c, 0);
		restack(&c->obj);
	} else {
		return 0;
	}
	wm.setclientlist = True;
	container_setdesk(c);
	return 1;
}

static void
containerfullscreen(struct Container *container, int fullscreen)
{
	if (fullscreen == REMOVE && !(container->state & FULLSCREEN))
		return;         /* already unset */
	if (fullscreen == ADD    &&  (container->state & FULLSCREEN))
		return;         /* already set */
	container->state ^= FULLSCREEN;
	container_update_geometry(container);
	redecorate(&container->obj);
	raise(container);
}

static void
containermaximize(struct Container *container, int maximize)
{
	if (container->state & FULLSCREEN)
		return;
	if (maximize == REMOVE && !(container->state & MAXIMIZED))
		return;         /* already unset */
	if (maximize == ADD    &&  (container->state & MAXIMIZED))
		return;         /* already set */
	container->state ^= MAXIMIZED;
	container_update_geometry(container);
	redecorate(&container->obj);
}

static void
containerminimize(struct Container *c, int minimize, int focus)
{
	if (minimize != REMOVE && !(c->state & MINIMIZED)) {
		c->state |= MINIMIZED;
		containerhide(c, 1);
		if (focus) {
			focusnext(c->mon, c->desk);
		}
	} else if (minimize != ADD && (c->state & MINIMIZED)) {
		c->state &= ~MINIMIZED;
		(void)containersendtodesk(c, wm.selmon, wm.selmon->seldesk);
		tabfocus(c->selcol->selrow->seltab);
		restack(&c->obj);
	}
}

static void
containershade(struct Container *c, int shade)
{
	int corner = config.corner + config.shadowthickness;

	if (c->state & FULLSCREEN)
		return;
	if (shade != REMOVE && !(c->state & SHADED)) {
		XDefineCursor(wm.display, c->borders[BORDER_NW], wm.cursors[CURSOR_W]);
		XDefineCursor(wm.display, c->borders[BORDER_SW], wm.cursors[CURSOR_W]);
		XDefineCursor(wm.display, c->borders[BORDER_NE], wm.cursors[CURSOR_E]);
		XDefineCursor(wm.display, c->borders[BORDER_SE], wm.cursors[CURSOR_E]);

		/*
		 * shrink corners to remove their inner shadow, so the
		 * top corners appear to merge with the bottom ones
		 */
		XResizeWindow(
			wm.display, c->borders[BORDER_NW],
			corner, config.corner - config.shadowthickness
		);
		XResizeWindow(
			wm.display, c->borders[BORDER_NE],
			corner, config.corner - config.shadowthickness
		);
	} else if (shade != ADD && (c->state & SHADED)) {
		XDefineCursor(wm.display, c->borders[BORDER_NW], wm.cursors[CURSOR_NW]);
		XDefineCursor(wm.display, c->borders[BORDER_SW], wm.cursors[CURSOR_SW]);
		XDefineCursor(wm.display, c->borders[BORDER_NE], wm.cursors[CURSOR_NE]);
		XDefineCursor(wm.display, c->borders[BORDER_SE], wm.cursors[CURSOR_SE]);

		/* revert changes above */
		XResizeWindow(wm.display, c->borders[BORDER_NW], corner, corner);
		XResizeWindow(wm.display, c->borders[BORDER_NE], corner, corner);
	} else {
		return;
	}
	c->state ^= SHADED;
	container_update_geometry(c);
	redecorate(&c->obj);
	if (is_focused(c)) {
		tabfocus(c->selcol->selrow->seltab);
	}
}

static void
containerstick(struct Container *c, int stick)
{
	if (stick != REMOVE && !(c->state & STICKY)) {
		c->state |= STICKY;
		container_setdesk(c);
	} else if (stick != ADD && (c->state & STICKY)) {
		c->state &= ~STICKY;
		(void)containersendtodesk(c, c->mon, c->mon->seldesk);
	}
}

static void
container_changelayer(struct Container *container, enum State state, int action)
{
	if (action == REMOVE && !(container->state & state))
		return;         /* already unset */
	if (action == ADD    &&  (container->state & state))
		return;         /* already set */
	container->state &= ABOVE|BELOW;
	container->state |= state;
	raise(container);
}

static void
containerdel(struct Object *self)
{
	struct Container *container = self->self;
	struct Object *col;
	int i;

	containerdelfocus(container);
	containerdelraise(container);
	TAILQ_REMOVE(&focus_history, &container->obj, entry);
	while ((col = TAILQ_FIRST(&container->colq)) != NULL)
		coldel(col->self);
	XDestroyWindow(wm.display, container->obj.frame);
	for (i = 0; i < BORDER_LAST; i++)
		XDestroyWindow(wm.display, container->borders[i]);
	free(container);
}

/* add column to container */
static void
containeraddcol(struct Container *c, struct Column *col, struct Column *prev)
{
	struct Container *oldc;

	oldc = col->c;
	col->c = c;
	c->selcol = col;
	c->ncols++;
	if (prev == NULL || TAILQ_EMPTY(&c->colq))
		TAILQ_INSERT_HEAD(&c->colq, &col->obj, entry);
	else
		TAILQ_INSERT_AFTER(&c->colq, &prev->obj, &col->obj, entry);
	XReparentWindow(wm.display, col->obj.win, c->obj.frame, 0, 0);
	containercalccols(c);
	if (oldc != NULL && oldc->ncols == 0) {
		containerdel(&oldc->obj);
	}
}

static struct Container *
containernew(int x, int y, int w, int h, enum State state)
{
	struct Container *c;
	int corner = config.corner + config.shadowthickness;
	int border = config.borderwidth - 1;
	struct border_tab { int window, cursor, gravity, x, y; } *table;

	c = emalloc(sizeof *c);
	*c = (struct Container) {
		.state = state,
		.layertop = &wm.layers[LAYER_NORMAL],
		.ishidden = 0,
		.obj.self = c,
		.obj.class = &container_class,
	};
	w += 2 * config.borderwidth;
	h += 2 * config.borderwidth + config.titlewidth;
	c->geometry.saved = c->geometry.current = (XRectangle){
		.x = x - config.borderwidth,
		.y = y - config.borderwidth,
		.width = w,
		.height = h,
	};
	table = (struct border_tab[BORDER_LAST]){
		{ BORDER_N,  CURSOR_N,  NorthWestGravity, 0, 0,                           },
		{ BORDER_W,  CURSOR_W,  NorthWestGravity, 0, border,                      },
		{ BORDER_S,  CURSOR_S,  SouthWestGravity, 0, h - config.borderwidth,      },
		{ BORDER_E,  CURSOR_E,  NorthEastGravity, w - config.borderwidth, border, },
		{ BORDER_SW, CURSOR_SW, SouthWestGravity, 0, h - corner,                  },
		{ BORDER_SE, CURSOR_SE, SouthEastGravity, w - corner, h - corner,         },
		{ BORDER_NW, CURSOR_NW, NorthWestGravity, 0, 0,                           },
		{ BORDER_NE, CURSOR_NE, NorthEastGravity, w - corner, 0,                  },
	};
	c->obj.frame = createwindow(
		wm.rootwin, c->geometry.current,
		CWEventMask, &(XSetWindowAttributes){
			.event_mask = EnterWindowMask,
		}
	);
	c->obj.frame = c->obj.frame;
	XGrabButton(
		wm.display, AnyButton, config.modifier,
		c->obj.frame, False, MOUSE_EVENTS,
		GrabModeSync, GrabModeAsync, None, None
	);
	context_add(c->obj.frame, &c->obj);
	TAILQ_INIT(&c->colq);
	for (size_t i = 0; i < BORDER_LAST; i++) {
		c->borders[table[i].window] = createdecoration(
			c->obj.frame,
			(XRectangle){
				/*
				 * Corners have fixed size, set it now.
				 * Edges are resized at will.
				 */
				table[i].x, table[i].y, corner, corner
			},
			wm.cursors[table[i].cursor], table[i].gravity
		);
		context_add(c->borders[table[i].window], &c->obj);
		XMapRaised(wm.display, c->borders[table[i].window]);
	}
	TAILQ_INSERT_AFTER(&wm.stacking_order, c->layertop, &c->obj, z_entry);
	containerinsertfocus(c);
	return c;
}

/* send container to desktop and focus another on the original desktop */
static void
containersendtodeskandfocus(struct Container *c, struct Monitor *mon, unsigned long d)
{
	struct Monitor *prevmon;
	int prevdesk, desk;

	if (c == NULL)
		return;
	prevmon = c->mon;
	prevdesk = c->desk;
	desk = d;

	/* is it necessary to send the container to the desktop */
	if (c->mon != mon || c->desk != desk) {
		/*
		 * Container sent to a desktop which is not the same
		 * as the one it was originally at.
		 */
		if (!containersendtodesk(c, mon, d)) {
			/*
			 * container could not be sent to given desktop;
			 */
			return;
		}
	}

	/* is it necessary to focus something? */
	if (mon == wm.selmon && desk == wm.selmon->seldesk) {
		/*
		 * Container sent to the focused desktop.
		 * Focus it, if visible.
		 */
		if (is_visible(c, mon, wm.selmon->seldesk)) {
			tabfocus(c->selcol->selrow->seltab);
		}
	} else if (prevmon == wm.selmon && prevdesk == wm.selmon->seldesk) {
		/*
		 * Container moved from the focused desktop.
		 * Focus the next visible container, if existing;
		 * or nothing, if there's no visible container.
		 */
		focusnext(mon, prevdesk);
	}
}

static void
container_configure(struct Object *self, unsigned int valuemask, XWindowChanges *wc)
{
	struct Container *container;

	if (self->class == &tab_class)
		container = ((struct Tab *)self)->row->col->c;
	else if (self->class == &container_class)
		container = self->self;
	else
		return;
	if (container->state & (FULLSCREEN|MINIMIZED|MAXIMIZED))
		return;
	if (valuemask & CWX)
		container->geometry.saved.x = wc->x;
	if (valuemask & CWY)
		container->geometry.saved.y = wc->y;
	if ((valuemask & CWWidth) && wc->width >= wm.minsize)
		container->geometry.saved.width = wc->width;
	if (!(container->state & SHADED) &&
	    (valuemask & CWHeight) && wc->height >= wm.minsize)
		container->geometry.saved.height = wc->height;
	containermoveresize(container, container->geometry.saved);
	redecorate(&container->obj);
}

static void
containersetstate(struct Object *obj, enum State state, int set)
{
	struct Container *container;
	struct Tab *tab;

	if (obj->class == &tab_class) {
		tab = obj->self;
		container = tab->row->col->c;
	} else if (obj->class == &container_class) {
		container = obj->self;
		tab = container->selcol->selrow->seltab;
	} else {
		return;
	}
	if (state & MAXIMIZED)
		containermaximize(container, set);
	if (state & FULLSCREEN)
		containerfullscreen(container, set);
	if (state & SHADED)
		containershade(container, set);
	if (state & STICKY)
		containerstick(container, set);
	if (state & MINIMIZED)
		containerminimize(container, set, is_focused(container));
	if (state & ABOVE)
		container_changelayer(container, ABOVE, set);
	if (state & BELOW)
		container_changelayer(container, BELOW, set);
	if (state & ATTENTION)
		tabupdateurgency(tab, set == ADD || (set == TOGGLE && !tab->isurgent));
	if (state & STRETCHED) {
		rowstretch(tab->row->col, tab->row);
		tabfocus(tab->row->seltab);
	}
	setstate_recursive(container);
}

/* attach tab into row; return nonzero if an attachment was performed */
static int
tabattach(struct Container *c, struct Tab *det, int x, int y)
{
	enum { CREATTAB = 0x0, CREATROW = 0x1, CREATCOL = 0x2 };
	struct Column *col, *ncol;
	struct Row *row, *nrow;
	struct Tab *tab;
	struct Object *l, *r, *t;
	int flag;

	if (c == NULL)
		return 0;
	flag = CREATTAB;
	col = NULL;
	row = NULL;
	tab = NULL;
	if (x < config.borderwidth) {
		flag = CREATCOL | CREATROW;
		goto found;
	}
	if (x >= c->geometry.current.width - config.borderwidth) {
		flag = CREATCOL | CREATROW;
		col = (void *)TAILQ_LAST(&c->colq, Queue);
		goto found;
	}
	TAILQ_FOREACH(l, &c->colq, entry) {
		col = l->self;
		if (TAILQ_NEXT(l, entry) != NULL && x >= col->x + col->w && x < col->x + col->w + config.divwidth) {
			flag = CREATCOL | CREATROW;
			goto found;
		}
		if (x >= col->x && x < col->x + col->w) {
			if (y < config.borderwidth) {
				flag = CREATROW;
				goto found;
			}
			if (y >= c->geometry.current.height - config.borderwidth) {
				flag = CREATROW;
				row = (void *)TAILQ_LAST(&col->rowq, Queue);
				goto found;
			}
			TAILQ_FOREACH(r, &col->rowq, entry) {
				row = r->self;
				if (y > row->y && y <= row->y + config.titlewidth) {
					TAILQ_FOREACH_REVERSE(t, &row->tabq, Queue, entry) {
						tab = t->self;
						if (x > col->x + tab->x + tab->w / 2) {
							flag = CREATTAB;
							goto found;
						}
					}
					tab = NULL;
					goto found;
				}
				if (TAILQ_NEXT(r, entry) != NULL && y >= row->y + row->h && y < row->y + row->h + config.divwidth) {
					flag = CREATROW;
					goto found;
				}
			}
		}
	}
	return 0;
found:
	ncol = NULL;
	nrow = NULL;
	if (flag & CREATCOL) {
		ncol = colnew();
		containeraddcol(c, ncol, col);
		col = ncol;
	}
	if (flag & CREATROW) {
		nrow = rownew();
		coladdrow(col, nrow, row);
		row = nrow;
	}
	rowaddtab(row, det, tab);
	if (ncol != NULL)
		containercalccols(c);
	else if (nrow != NULL)
		colcalcrows(col, 1);
	else
		rowcalctabs(row);
	tabfocus(det);
	raise(c);
	XMapSubwindows(wm.display, c->obj.frame);
	/* no need to call set_container_group(); tabfocus() already calls them */
	set_window_desktop(det->obj.win, c->desk);
	update_tiles(c);
	redecorate(&c->obj);
	return 1;
}

static void
containerdelrow(struct Row *row)
{
	struct Container *c;
	struct Column *col;
	int recalc;

	/* delete row; then column if empty; then container if empty */
	col = row->col;
	c = col->c;
	recalc = 1;
	if (row->ntabs == 0)
		rowdel(row);
	if (col->nrows == 0)
		coldel(col);
	if (c->ncols == 0) {
		containerdel(&c->obj);
		recalc = 0;
	}
	if (recalc) {
		update_tiles(c);
		set_container_group(c);
	}
}

static void
tabdetach(struct Tab *tab, int x, int y)
{
	struct Row *row;

	row = tab->row;
	tabremove(row, tab);
	XReparentWindow(wm.display, tab->title, wm.rootwin, x, y);
	rowcalctabs(row);
}

static void
containernewwithtab(struct Tab *tab, struct Monitor *mon, int desk, XRectangle rect, enum State state)
{
	struct Container *c;
	struct Column *col;
	struct Row *row;

	if (tab == NULL)
		return;
	c = containernew(rect.x, rect.y, rect.width, rect.height, state);
	c->mon = mon;
	c->desk = desk;
	row = rownew();
	col = colnew();
	containeraddcol(c, col, NULL);
	coladdrow(col, row, NULL);
	rowaddtab(row, tab, NULL);
	redecorate(&c->obj);
	XMapSubwindows(wm.display, c->obj.frame);
	containerplace(c, mon, desk, (state & USERPLACED));
	container_update_geometry(c);
	if (is_visible(c, wm.selmon, wm.selmon->seldesk)) {
		raise(c);
		containerhide(c, 0);
		tabfocus(tab);
	}
	/* no need to call and set_container_group(); tabfocus() already calls them */
	container_setdesk(c);
	wm.setclientlist = True;
}

static struct Tab *
getleaderof(Window leader)
{
	struct Object *c, *obj;
	struct Tab *tab;

	if (leader == None)
		return NULL;
	if (wm.focused != NULL && wm.focused->class == &tab_class) {
		tab = wm.focused->self;
		if (tab->obj.win == leader || tab->leader == leader)
			return tab;
	}
	if ((obj = context_get(leader)) != NULL && obj->class == &tab_class) {
		tab = obj->self;
		return TAILQ_LAST(&tab->row->tabq, Queue)->self;
	}
	TAILQ_FOREACH(c, &focus_history, entry)
		TAB_FOREACH((struct Container *)c, tab)
			if (tab->leader == leader)
				return TAILQ_LAST(&tab->row->tabq, Queue)->self;
	return NULL;
}

static struct Tab *
gettabfrompid(unsigned long pid)
{
	struct Object *c;
	struct Tab *tab;

	if (pid <= 1)
		return NULL;
	if (wm.focused != NULL && wm.focused->class == &tab_class) {
		tab = wm.focused->self;
		if (tab->pid == pid)
			return tab;
	}
	TAILQ_FOREACH(c, &focus_history, entry)
		TAB_FOREACH((struct Container *)c, tab)
			if (pid == tab->pid)
				return TAILQ_LAST(&tab->row->tabq, Queue)->self;
	return NULL;
}

static void
managecontainer(struct Object *app, struct Monitor *mon, int desk, Window win, Window leader, XRectangle rect, enum State state)
{
	struct Tab *tab, *prev;
	struct Container *c;
	struct Row *row;

	(void)app;
	if ((prev = getleaderof(leader)) == NULL)
		prev = gettabfrompid(getcardprop(wm.display, win, wm.atoms[_NET_WM_PID]));
	tab = tabnew(win, leader);
	winupdatetitle(tab->obj.win, &tab->name);
	if (prev == NULL) {
		containernewwithtab(tab, mon, desk, rect, state);
	} else {
		row = prev->row;
		c = row->col->c;
		rowaddtab(row, tab, prev);
		rowcalctabs(row);
		set_window_desktop(win, c->desk);
		wm.setclientlist = True;
		update_tiles(c);
		redecorate(&c->obj);
		XMapSubwindows(wm.display, c->obj.frame);
		if (is_focused(c)) {
			tabfocus(tab);
		}
	}
}

static void
unmanagetab(struct Object *obj)
{
	struct Tab *tab = obj->self;
	struct Row *row = tab->row;
	struct Column *col = row->col;
	struct Container *container = col->c;
	Bool refocus = False;

	refocus = False;
	tabdel(tab);
	if (row->ntabs == 0) {
		rowdel(row);
		if (col->nrows == 0) {
			coldel(col);
			if (container->ncols == 0) {
				if (is_focused(container))
					refocus = True;
				containerdel(&container->obj);
				goto done;
			}
		}
	}
	update_tiles(container);
	redecorate(&container->obj);
	set_container_group(container);
done:
	if (refocus)
		focusnext(container->mon, container->desk);
	wm.setclientlist = True;
}

static Bool
snappable(int a, int b)
{
	return abs(a - b) < config.snap;
}

static XRectangle
snap(XRectangle potential)
{
	struct Object *obj;
	XRectangle final = potential;

	if (config.snap <= 0)
		return final;
	TAILQ_FOREACH(obj, &focus_history, entry) {
		struct Container *container = obj->self;
		XRectangle *barrier = &container->geometry.current;

		if (!is_visible(container, wm.selmon, wm.selmon->seldesk))
			continue;

		/*
		 * If projections intersect vertically, snap north/south edge
		 * of final rectangle to the opposite edge (south/north) of
		 * barrier rectangle if they are close enough.
		 *                     ┌────────┐
		 *                     │        │
		 *                     │        │
		 *                     │        │
		 *                     └───╼━━━━┙
		 *                         ┆    ┆
		 *                         ┆    ┆
		 *                         ┍━━━━╾─┐
		 *                         │      │
		 *                         │      │
		 *                         └──────┘
		 */
		if (final.x + final.width >= barrier->x &&
		    final.x <= barrier->x + barrier->width) {
			if (snappable(potential.y + potential.height, barrier->y))
				final.y = barrier->y - final.height;
			if (snappable(potential.y, barrier->y + barrier->height))
				final.y = barrier->y + barrier->height;
		}

		/*
		 * If projections intersect horizontally, snap west/east edge
		 * of final rectangle to the opposite edge (east/west) of
		 * barrier rectangle if they are close enough.
		 *                ┌────────┐
		 *                │        │
		 *                │        ╽┄┄┄┎──────┐
		 *                │        ┃   ┃      │
		 *                └────────┚┄┄┄╿      │
		 *                             └──────┘
		 */
		if (final.y + final.height >= barrier->y &&
		    final.y <= barrier->y + barrier->height) {
			if (snappable(potential.x + potential.width, barrier->x))
				final.x = barrier->x - final.width;
			if (snappable(potential.x, barrier->x + barrier->width))
				final.x = barrier->x + barrier->width;
		}

		/*
		 * If rectangles touch vertically, snap their west edges
		 * together (or their east edges) if they are close enough.
		 *                     ┌────────┐
		 *                     │        │
		 *                     │        │
		 *                     │        │
		 *                     └───┮━━━━┵─┐
		 *                         │      │
		 *                         │      │
		 *                         └──────┘
		 */
		if (final.y == barrier->y + barrier->height ||
		    final.y + final.height == barrier->y) {
			if (snappable(potential.x, barrier->x))
				final.x = barrier->x;
			if (snappable(potential.x + potential.width, barrier->x + barrier->width))
				final.x = barrier->x + barrier->width - final.width;
		}

		/*
		 * If rectangles touch vertically, snap their north edges
		 * together (or their south edges) if they are close enough.
		 *                 ┌────────┐
		 *                 │        │
		 *                 │        ┟──────┐
		 *                 │        ┃      │
		 *                 └────────┦      │
		 *                          └──────┘
		 */
		if (final.x == barrier->x + barrier->width ||
		    final.x + final.width == barrier->x) {
			if (snappable(potential.y, barrier->y))
				final.y = barrier->y;
			if (snappable(potential.y + potential.height, barrier->y + barrier->height))
				final.y = barrier->y + barrier->height - final.height;
		}
	}

	/* snap rectangle to monitor edges */
	if (snappable(potential.x, wm.selmon->window_area.x))
		final.x = wm.selmon->window_area.x;
	if (snappable(potential.x + potential.width, wm.selmon->window_area.x + wm.selmon->window_area.width))
		final.x = wm.selmon->window_area.x +
		wm.selmon->window_area.width - final.width;
	if (snappable(potential.y, wm.selmon->window_area.y))
		final.y = wm.selmon->window_area.y;
	if (snappable(potential.y + potential.height, wm.selmon->window_area.y + wm.selmon->window_area.height))
		final.y = wm.selmon->window_area.y +
		wm.selmon->window_area.height - final.height;
	return final;
}

static void
drag_move(struct Container *container, int xroot, int yroot)
{
	XEvent event;
	XRectangle geometry;

	if (container->state & (FULLSCREEN|MINIMIZED))
		return;
	if (XGrabPointer(
		wm.display, container->obj.frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, wm.cursors[CURSOR_MOVE], CurrentTime
	) != GrabSuccess)
		return;
	geometry = container->geometry.saved;
	for (;;) {
		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		if (!(container->state & MAXIMIZED) && event.xmotion.y_root <= 0) {
			containersetstate(&container->obj, MAXIMIZED, ADD);
		} else if (!(container->state & MAXIMIZED)) {
			container->geometry.saved.x += event.xmotion.x_root - xroot;
			container->geometry.saved.y += event.xmotion.y_root - yroot;
			geometry = snap(container->geometry.saved);
			containermoveresize(container, geometry);
		} else if (event.xmotion.y_root > config.titlewidth) {
			container->geometry.saved.x = event.xmotion.x_root - container->geometry.saved.width / 2;
			container->geometry.saved.y = 0;
			containersetstate(&container->obj, MAXIMIZED, REMOVE);
		}
		xroot = event.xmotion.x_root;
		yroot = event.xmotion.y_root;
	}
	XUngrabPointer(wm.display, CurrentTime);
}

static void
drag_resize(struct Container *container, int border, int xroot, int yroot)
{
	enum {
		TOP    = (1 << 0),
		BOTTOM = (1 << 1),
		LEFT   = (1 << 2),
		RIGHT  = (1 << 3),
	};
	Cursor cursor;
	XEvent event;
	XMotionEvent *motion = &event.xmotion;
	int x, y;
	int direction;

	if (container->state & (FULLSCREEN|MINIMIZED|MAXIMIZED))
		return;
	if (container->state & SHADED) {
		if (border == BORDER_SW || border == BORDER_NW)
			border = BORDER_W;
		else if (border == BORDER_SE || border == BORDER_NE)
			border = BORDER_E;
		else if (border == BORDER_N || border == BORDER_S)
			return;
	}
	switch (border) {
	case BORDER_NW:
		direction = TOP | LEFT;
		cursor = wm.cursors[CURSOR_NW];
		break;
	case BORDER_NE:
		direction = TOP | RIGHT;
		cursor = wm.cursors[CURSOR_NE];
		break;
	case BORDER_N:
		direction = TOP;
		cursor = wm.cursors[CURSOR_N];
		break;
	case BORDER_SW:
		direction = BOTTOM | LEFT;
		cursor = wm.cursors[CURSOR_SW];
		break;
	case BORDER_SE:
		direction = BOTTOM | RIGHT;
		cursor = wm.cursors[CURSOR_SE];
		break;
	case BORDER_S:
		direction = BOTTOM;
		cursor = wm.cursors[CURSOR_S];
		break;
	case BORDER_W:
		direction = LEFT;
		cursor = wm.cursors[CURSOR_W];
		break;
	case BORDER_E:
		direction = RIGHT;
		cursor = wm.cursors[CURSOR_E];
		break;
	}
	if (direction & LEFT)
		x = xroot - container->geometry.saved.x;
	else if (direction & RIGHT)
		x = container->geometry.saved.x + container->geometry.saved.width - xroot;
	else
		x = 0;
	if (direction & TOP)
		y = yroot - container->geometry.saved.y;
	else if (direction & BOTTOM)
		y = container->geometry.saved.y + container->geometry.saved.height - yroot;
	else
		y = 0;
	if (XGrabPointer(
		wm.display, container->obj.frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, cursor, CurrentTime
	) != GrabSuccess)
		return;
	for (;;) {
		int dx, dy;

		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		if (x > container->geometry.saved.width) x = 0;
		if (y > container->geometry.saved.height) y = 0;
		if (direction & LEFT &&
		    ((motion->x_root < xroot && x > motion->x_root - container->geometry.saved.x) ||
		     (motion->x_root > xroot && x < motion->x_root - container->geometry.saved.x))) {
			dx = xroot - motion->x_root;
			if (container->geometry.saved.width + dx < wm.minsize) continue;
			container->geometry.saved.x -= dx;
			container->geometry.saved.width += dx;
		} else if (direction & RIGHT &&
		    ((motion->x_root > xroot && x > container->geometry.saved.x + container->geometry.saved.width - motion->x_root) ||
		     (motion->x_root < xroot && x < container->geometry.saved.x + container->geometry.saved.width - motion->x_root))) {
			dx = motion->x_root - xroot;
			if (container->geometry.saved.width + dx < wm.minsize) continue;
			container->geometry.saved.width += dx;
		}
		if (direction & TOP &&
		    ((motion->y_root < yroot && y > motion->y_root - container->geometry.saved.y) ||
		     (motion->y_root > yroot && y < motion->y_root - container->geometry.saved.y))) {
			dy = yroot - motion->y_root;
			if (container->geometry.saved.height + dy < wm.minsize) continue;
			container->geometry.saved.y -= dy;
			container->geometry.saved.height += dy;
		} else if (direction & BOTTOM &&
		    ((motion->y_root > yroot && container->geometry.saved.y + container->geometry.saved.height - motion->y_root < y) ||
		     (motion->y_root < yroot && container->geometry.saved.y + container->geometry.saved.height - motion->y_root > y))) {
			dy = motion->y_root - yroot;
			if (container->geometry.saved.height + dy < wm.minsize) continue;
			container->geometry.saved.height += dy;
		}
		xroot = motion->x_root;
		yroot = motion->y_root;
		containermoveresize(container, container->geometry.saved);
	}
	XUngrabPointer(wm.display, CurrentTime);
}

static void
drag_tab(struct Tab *tab, int xroot, int yroot, int x, int y)
{
#define DND_POS 10      /* pixels from pointer cursor to drag-and-drop icon */
	struct Monitor *mon;
	struct Object *obj;
	struct Row *row;        /* row to be deleted, if emptied */
	struct Container *container;
	Window win;
	XEvent event;

	row = tab->row;
	container = row->col->c;
	if (container->state & (FULLSCREEN|MINIMIZED))
		return;
	if (XGrabPointer(
		wm.display, wm.rootwin, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime
	) != GrabSuccess)
		return;
	tabdetach(tab, xroot - x, yroot - y);
	update_tiles(container);
	XUnmapWindow(wm.display, tab->title);
	XMoveWindow(
		wm.display, wm.dragwin,
		xroot - DND_POS - (2 * config.borderwidth + config.titlewidth),
		yroot - DND_POS - (2 * config.borderwidth + config.titlewidth)
	);
	XRaiseWindow(wm.display, wm.dragwin);
	for (;;) {
		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		XMoveWindow(
			wm.display, wm.dragwin,
			event.xmotion.x_root - DND_POS - (2 * config.borderwidth + config.titlewidth),
			event.xmotion.y_root - DND_POS - (2 * config.borderwidth + config.titlewidth)
		);
	}
	XMoveWindow(
		wm.display, wm.dragwin,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth)
	);
	xroot = event.xbutton.x_root - x;
	yroot = event.xbutton.y_root - y;
	obj = context_get(event.xbutton.subwindow);
	container = NULL;
	if (obj != NULL && obj->class == &container_class) {
		container = obj->self;
		XTranslateCoordinates(
			wm.display, event.xbutton.window, container->obj.frame,
			event.xbutton.x_root, event.xbutton.y_root, &x,
			&y, &win
		);
	}
	if (row->col->c != container) {
		XUnmapWindow(wm.display, tab->obj.frame);
		XReparentWindow(wm.display, tab->obj.frame, wm.rootwin, x, y);
	}
	if (!tabattach(container, tab, x, y)) {
		mon = getmon(x, y);
		if (mon == NULL)
			mon = wm.selmon;
		containernewwithtab(
			tab, mon, mon->seldesk,
			(XRectangle){
				.x = xroot - config.titlewidth,
				.y = yroot,
				.width = tab->winw,
				.height = tab->winh,
			},
			USERPLACED
		);
	}
	containerdelrow(row);
	set_active_window(tab->obj.win);
	XUngrabPointer(wm.display, CurrentTime);
}

static void
tab_btnpress(struct Object *self, XButtonPressedEvent *press)
{
	struct Tab *tab;
	struct Container *container;

	if (self->class == &tab_class)
		tab = (struct Tab *)self;
	else if (self->class == &dialog_class)
		tab = ((struct Dialog *)self)->tab;
	else
		return;
	container = tab->row->col->c;

	if (press->button == Button1 && press->window != tab->close_btn &&
	    (&tab->obj != wm.focused))
		tabfocus(tab);
	if (press->button == Button1 &&
	    &container->obj != TAILQ_NEXT(container->layertop, z_entry))
		raise(container);
	if (press->window == tab->title && press->button == Button1 && press->serial == 2) {
		rowstretch(tab->row->col, tab->row);
	} else if (press->window == tab->title && press->button == Button1) {
		drag_move(container, press->x_root, press->y_root);
	} else if (press->window == tab->title && press->button == Button3) {
		drag_tab(tab, press->x_root, press->y_root, press->x, press->y);
	} else if (press->window == tab->title && press->button == Button4) {
		containersetstate(&container->obj, SHADED, ADD);
	} else if (press->window == tab->title && press->button == Button5) {
		containersetstate(&container->obj, SHADED, REMOVE);
	} else if (press->window == tab->close_btn && press->button == Button1) {
		if (released_inside(wm.display, press))
			window_close(wm.display, tab->obj.win);
		return;
	}
}

static void
coldiv_btnpress(struct Object *self, XButtonPressedEvent *press)
{
	XEvent event;
	struct Column *thiscol = self->self;
	struct Column *nextcol = (void *)TAILQ_NEXT(self, entry);
	struct Container *container = thiscol->c;
	int x_prev = thiscol->x + press->x;

	if (press->button != Button1)
		return;
	if (container->state & (FULLSCREEN|MINIMIZED))
		return;
	if (XGrabPointer(
		wm.display, container->obj.frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, wm.cursors[CURSOR_H], CurrentTime
	) != GrabSuccess)
		return;
	for (;;) {
		int width;
		double fact;

		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		width = containercontentwidth(container);
		fact = (double)(event.xmotion.x - x_prev) / (double)width;
		if ((thiscol->fact + fact) * width >= wm.minsize &&
		    (nextcol->fact - fact) * width >= wm.minsize) {
			thiscol->fact += fact;
			nextcol->fact -= fact;
			update_tiles(container);
		}
		x_prev = event.xmotion.x;
	}
	XUngrabPointer(wm.display, CurrentTime);
}

static void
rowdiv_btnpress(struct Object *self, XButtonPressedEvent *press)
{
	XEvent event;
	struct Row *thisrow = self->self;
	struct Row *nextrow = (void *)TAILQ_NEXT(self, entry);
	struct Container *container = thisrow->col->c;
	int y_prev = thisrow->y + press->y;

	if (press->button != Button1)
		return;
	if (container->state & (FULLSCREEN|MINIMIZED))
		return;
	if (XGrabPointer(
		wm.display, container->obj.frame, False,
		ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync,
		None, wm.cursors[CURSOR_V], CurrentTime
	) != GrabSuccess)
		return;
	for (;;) {
		int dy, height;
		double fact;

		XMaskEvent(wm.display, ButtonReleaseMask|PointerMotionMask, &event);
		if (event.type == ButtonRelease)
			break;
		if (event.type != MotionNotify)
			continue;
		dy = event.xmotion.y - y_prev;
		height = columncontentheight(thisrow->col);
		fact = (double)dy / (double)height;
		if (dy < 0 && event.xmotion.y + press->y < nextrow->y) for (
			struct Object *obj = &thisrow->obj;
			obj != NULL;
			obj = TAILQ_PREV(obj, Queue, entry)
		) {
			struct Row *row = obj->self;
			if (row->fact <= 0.0)
				continue;
			if (row->fact + fact <= 0.0)
				fact = -row->fact;
			row->fact += fact;
			nextrow->fact -= fact;
			break;
		} else if (dy > 0 && event.xmotion.y + press->y > nextrow->y) for (
			struct Object *obj = &nextrow->obj;
			obj != NULL;
			obj = TAILQ_NEXT(obj, entry)
		) {
			struct Row *row = obj->self;
			if (row->fact <= 0.0) continue;
			if (row->fact - fact <= 0.0)
				fact = row->fact;
			thisrow->fact += fact;
			row->fact -= fact;
			break;
		}
		update_tiles(container);
		y_prev = event.xmotion.y;
	}
	XUngrabPointer(wm.display, CurrentTime);
}

static void
container_btnpress(struct Object *self, XButtonPressedEvent *press)
{
	struct Container *container = self->self;
	struct Tab *seltab = container->selcol->selrow->seltab;

	if (container->state & (FULLSCREEN|MINIMIZED))
		return;
	if (press->button == Button1 && press->window != seltab->close_btn &&
	    (&seltab->obj != wm.focused))
		tabfocus(seltab);
	if (press->button == Button1 &&
	    &container->obj != TAILQ_NEXT(container->layertop, z_entry))
		raise(container);
	if (isvalidstate(press->state) && press->button == Button1) {
		drag_move(container, press->x_root, press->y_root);
	} else if (isvalidstate(press->state) && press->button == Button3) {
		enum border border;

		if (press->x <= container->geometry.current.width/2 && press->y <= container->geometry.current.height/2)
			border = BORDER_NW;
		else if (press->x > container->geometry.current.width/2 && press->y <= container->geometry.current.height/2)
			border = BORDER_NE;
		else if (press->x <= container->geometry.current.width/2 && press->y > container->geometry.current.height/2)
			border = BORDER_SW;
		else
			border = BORDER_SE;
		drag_resize(
			container, border,
			press->x_root, press->y_root
		);
	} else for (enum border border = 0; border < BORDER_LAST; border++) {
		if (press->window != container->borders[border])
			continue;
		if (press->button == Button1) {
			drag_resize(
				container, border,
				press->x_root, press->y_root
			);
		} else if (press->button == Button3) {
			drag_move(
				container,
				press->x_root, press->y_root
			);
		}
		break;
	}
}

static int
isurgent(Window win)
{
	XWMHints *wmh;
	int ret;

	ret = 0;
	if ((wmh = XGetWMHints(wm.display, win)) != NULL) {
		ret = wmh->flags & XUrgencyHint;
		XFree(wmh);
	}
	return ret;
}

static void
init(void)
{
	TAILQ_INIT(&focus_history);
}

static void
clean(void)
{
	struct Object *obj;

	while ((obj = TAILQ_FIRST(&focus_history)) != NULL)
		containerdel(obj);
	for (size_t style = 0; style < LEN(decorations); style++) {
		XFreePixmap(wm.display, decorations[style].bar_vert);
		XFreePixmap(wm.display, decorations[style].bar_horz);
		XFreePixmap(wm.display, decorations[style].corner_nw);
		XFreePixmap(wm.display, decorations[style].corner_ne);
		XFreePixmap(wm.display, decorations[style].corner_sw);
		XFreePixmap(wm.display, decorations[style].corner_se);
	}

}

static void
monitor_delete(struct Monitor *mon)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &focus_history, entry) {
		struct Container *container = obj->self;
		if (container->mon == mon)
			container->mon = NULL;
	}
}

static void
monitor_reset(void)
{
	struct Object *obj;
	struct Container *refocus = NULL;

	TAILQ_FOREACH(obj, &focus_history, entry) {
		struct Container *container = obj->self;
		if (!(container->state & MINIMIZED) && container->mon == NULL) {
			if (is_focused(container))
				refocus = container;
			container->mon = wm.selmon;
			container->desk = wm.selmon->seldesk;
			containerplace(container, wm.selmon, wm.selmon->seldesk, 0);
			container_setdesk(container);
			setstate_recursive(container);
		}
		container_update_geometry(container);
		redecorate(obj);
	}
	if (refocus != NULL)
		tabfocus(refocus->selcol->selrow->seltab);
}

static void
redecorate_all(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &focus_history, entry)
		redecorate(obj);
}

static void
reload_theme(void)
{
	XRectangle rects[config.shadowthickness * 4];
	int wholesize = config.corner + config.shadowthickness;

	for (int style = 0; style < STYLE_LAST; style++) {
		int x, y;
		unsigned long top = wm.theme.colors[style][COLOR_LIGHT].pixel;
		unsigned long bot = wm.theme.colors[style][COLOR_DARK].pixel;

		/* background pixmap of horizontal (north and south) borders */
		updatepixmap(
			&decorations[style].bar_horz,
			NULL, NULL, 1, config.borderwidth
		);
		drawshadow(
			decorations[style].bar_horz,
			-config.shadowthickness, 0,
			config.borderwidth, config.borderwidth, style
		);

		/* background pixmap of vertical (west and east) borders */
		updatepixmap(
			&decorations[style].bar_vert,
			NULL, NULL, config.borderwidth, 1
		);
		drawshadow(
			decorations[style].bar_vert,
			0, -config.shadowthickness,
			config.borderwidth, config.borderwidth, style
		);

		/*
		 * Background pixmap of northwest corner.
		 * Corners' shadows are complex to draw, for corners are
		 * not a rectangle, but a 6-vertices polygon shaped like
		 * the uppercase greek letter Gamma.
		 */
		updatepixmap(
			&decorations[style].corner_nw,
			NULL, NULL, wholesize, wholesize
		);
		x = y = 0;
		drawbackground(
			decorations[style].corner_nw,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 2 + 0] = (XRectangle){.x = x + i, .y = y + 0, .width = 1,                     .height = config.corner - 1 - i};
			rects[i * 2 + 1] = (XRectangle){.x = x + 0, .y = y + i, .width = config.corner - 1 - i, .height = 1};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){ .foreground = top });
		XFillRectangles(
			wm.display, decorations[style].corner_nw,
			wm.gc, rects, config.shadowthickness * 2
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 4 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = 1,                         .height = config.titlewidth + 1 + i};
			rects[i * 4 + 1] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.borderwidth - 1 - i, .width = config.titlewidth + 1 + i, .height = 1};
			rects[i * 4 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + i,                          .width = 1,                         .height = config.borderwidth - i};
			rects[i * 4 + 3] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i,      .width = config.borderwidth - i,    .height = 1};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){ .foreground = bot });
		XFillRectangles(
			wm.display, decorations[style].corner_nw,
			wm.gc, rects, config.shadowthickness * 4
		);
		/*
		 * In addition to the corner's shadows, the shadows of
		 * the neighbouring borders are part of the corner.
		 * That is a optimization hack so we do not need to
		 * redraw the borders whenever the container resizes.
		 * The tip of each border are then "anchored" to the
		 * corners.
		 */
		drawshadow(
			decorations[style].corner_nw,
			0, config.corner,
			config.borderwidth, config.borderwidth, style
		);
		drawshadow(
			decorations[style].corner_nw,
			config.corner, 0,
			config.borderwidth, config.borderwidth, style
		);

		/* bottom left corner */
		updatepixmap(
			&decorations[style].corner_sw,
			NULL, NULL, wholesize, wholesize
		);
		x = 0;
		y = config.shadowthickness;
		drawbackground(
			decorations[style].corner_sw,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + i,                          .y = y + 0,                     .width = 1,                          .height = config.corner - 1 - i};
			rects[i * 3 + 1] = (XRectangle){.x = x + 0,                          .y = y + i,                     .width = config.borderwidth - 1 - i, .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + config.titlewidth + i, .width = config.titlewidth,          .height = 1};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){.foreground = top });
		XFillRectangles(
			wm.display, decorations[style].corner_sw,
			wm.gc, rects, config.shadowthickness * 3
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + config.borderwidth - 1 - i, .y = y + i,                     .width = 1,                 .height = config.titlewidth};
			rects[i * 3 + 1] = (XRectangle){.x = x + i,                          .y = y + config.corner - 1 - i, .width = config.corner - i, .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.corner - 1 - i,      .y = y + config.titlewidth + i, .width = 1,                 .height = config.borderwidth - i};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){.foreground = bot});
		XFillRectangles(
			wm.display, decorations[style].corner_sw,
			wm.gc, rects, config.shadowthickness * 3
		);
		drawshadow(
			decorations[style].corner_sw,
			0, config.shadowthickness - config.borderwidth,
			config.borderwidth, config.borderwidth, style
		);
		drawshadow(
			decorations[style].corner_sw,
			wholesize - config.shadowthickness,
			wholesize - config.borderwidth,
			config.borderwidth, config.borderwidth, style
		);

		/* top right corner */
		updatepixmap(
			&decorations[style].corner_ne,
			NULL, NULL, wholesize, wholesize
		);
		x = config.shadowthickness;
		y = 0;
		drawbackground(
			decorations[style].corner_ne,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + i,                     .y = y + 0,                          .width = 1,                     .height = config.borderwidth - 1 - i};
			rects[i * 3 + 1] = (XRectangle){.x = x + 0,                     .y = y + i,                          .width = config.corner - 1 - i, .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.borderwidth - 1 - i, .width = 1,                     .height = config.titlewidth};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){.foreground = top});
		XFillRectangles(
			wm.display, decorations[style].corner_ne,
			wm.gc, rects, config.shadowthickness * 3
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 3 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                          .width = 1,                      .height = config.corner - i};
			rects[i * 3 + 1] = (XRectangle){.x = x + i,                     .y = y + config.borderwidth - 1 - i, .width = config.titlewidth,      .height = 1};
			rects[i * 3 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + config.corner - 1 - i,      .width = config.borderwidth - i, .height = 1};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){.foreground = bot});
		XFillRectangles(
			wm.display, decorations[style].corner_ne,
			wm.gc, rects, config.shadowthickness * 3
		);
		drawshadow(
			decorations[style].corner_ne,
			wholesize - config.borderwidth, config.corner,
			config.borderwidth, config.borderwidth, style
		);
		drawshadow(
			decorations[style].corner_ne,
			config.shadowthickness - config.borderwidth, 0,
			config.borderwidth, config.borderwidth, style
		);

		/* bottom right corner */
		updatepixmap(
			&decorations[style].corner_se,
			NULL, NULL, wholesize, wholesize
		);
		x = config.shadowthickness;
		y = config.shadowthickness;
		drawbackground(
			decorations[style].corner_se,
			0, 0, wholesize, wholesize, style
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 4 + 0] = (XRectangle){.x = x + i,                     .y = y + config.titlewidth + i,  .width = 1,                              .height = config.borderwidth - 1 - i * 2};
			rects[i * 4 + 1] = (XRectangle){.x = x + config.titlewidth + i, .y = y + i,                      .width = config.borderwidth - 1 - i * 2, .height = 1};
			rects[i * 4 + 2] = (XRectangle){.x = x + config.titlewidth + i, .y = y + i,                      .width = 1,                              .height = config.titlewidth + 1};
			rects[i * 4 + 3] = (XRectangle){.x = x + i,                     .y = y + config.titlewidth + i,  .width = config.titlewidth + 1,          .height = 1};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){.foreground = top});
		XFillRectangles(
			wm.display, decorations[style].corner_se,
			wm.gc, rects, config.shadowthickness * 4
		);
		for (int i = 0; i < config.shadowthickness; i++) {
			rects[i * 2 + 0] = (XRectangle){.x = x + config.corner - 1 - i, .y = y + i,                     .width = 1,                      .height = config.corner - i};
			rects[i * 2 + 1] = (XRectangle){.x = x + i,                     .y = y + config.corner - 1 - i, .width = config.corner - i,      .height = 1};
		}
		XChangeGC(wm.display, wm.gc, GCForeground, &(XGCValues){.foreground = bot});
		XFillRectangles(
			wm.display, decorations[style].corner_se,
			wm.gc, rects, config.shadowthickness * 2
		);
		drawshadow(
			decorations[style].corner_se,
			wholesize - config.borderwidth,
			config.shadowthickness - config.borderwidth,
			config.borderwidth, config.borderwidth, style
		);
		drawshadow(
			decorations[style].corner_se,
			config.shadowthickness - config.borderwidth,
			wholesize - config.borderwidth,
			config.borderwidth, config.borderwidth, style
		);
	}
}

static void
show_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &focus_history, entry)
		if (!(((struct Container *)obj)->state & MINIMIZED))
			containerhide(obj->self, ADD);
}

static void
hide_desktop(void)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &focus_history, entry)
		if (!(((struct Container *)obj)->state & MINIMIZED))
			containerhide(obj->self, REMOVE);
}

static void
change_desktop(struct Monitor *mon, int desk_old, int desk_new)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &focus_history, entry) {
		struct Container *container = obj->self;

		if (container->mon != mon)
			continue;
		if (!(container->state & MINIMIZED) && container->desk == desk_new) {
			containerhide(container, REMOVE);
		} else if (!(container->state & STICKY) && container->desk == desk_old) {
			containerhide(container, ADD);
		}
	}
}

static void
handle_property(struct Object *self, Atom property)
{
	struct Tab *tab = self->self;

	if (property == XA_WM_NAME || property == wm.atoms[_NET_WM_NAME]) {
		winupdatetitle(tab->obj.win, &tab->name);
		tabdecorate(tab, get_style(tab->row->col->c));
	} else if (property == XA_WM_HINTS) {
		tabupdateurgency(tab, isurgent(tab->obj.win));
	}
}

static void
handle_enter(struct Object *self)
{
	struct Tab *tab;
	struct Container *container;

	if (self->class == &tab_class)
		tab = (struct Tab *)self;
	else if (self->class == &dialog_class)
		tab = ((struct Dialog *)self)->tab;
	else if (self->class == &container_class)
		tab = ((struct Container *)self)->selcol->selrow->seltab;
	else
		return;
	container = tab->row->col->c;
	if (is_focused(container) && config.sloppytiles)
		tabfocus(tab);
	if (!is_focused(container) && config.sloppyfocus)
		tabfocus(container->selcol->selrow->seltab);
}

static enum State
atoms2statemask(Atom states[2])
{
	enum State state = 0;

	/*
	 * EWMH allows to set two states for a client in a single client
	 * message.  This is a hack to allow window maximization; for it
	 * does not define a state for fully maximized windows.  Rather,
	 * EWMH defines two partial states: horizontally, and vertically
	 * maximized.  A fully maximized window should be represented as
	 * having both states set.
	 *
	 * Since shod does not do partial maximization, we normalize
	 * either as an internal MAXIMIZED state.
	 */
	for (size_t i = 0; i < 2; i++) {
		if (states[i] == wm.atoms[_NET_WM_STATE_MAXIMIZED_HORZ])
			state |= MAXIMIZED;
		else if (states[i] == wm.atoms[_NET_WM_STATE_MAXIMIZED_VERT])
			state |= MAXIMIZED;
		else if (states[i] == wm.atoms[_NET_WM_STATE_ABOVE])
			state |= ABOVE;
		else if (states[i] == wm.atoms[_NET_WM_STATE_BELOW])
			state |= BELOW;
		else if (states[i] == wm.atoms[_NET_WM_STATE_FULLSCREEN])
			state |= FULLSCREEN;
		else if (states[i] == wm.atoms[_NET_WM_STATE_HIDDEN])
			state |= MINIMIZED;
		else if (states[i] == wm.atoms[_NET_WM_STATE_SHADED])
			state |= SHADED;
		else if (states[i] == wm.atoms[_NET_WM_STATE_STICKY])
			state |= STICKY;
		else if (states[i] == wm.atoms[_NET_WM_STATE_DEMANDS_ATTENTION])
			state |= ATTENTION;
		else if (states[i] == wm.atoms[_SHOD_WM_STATE_STRETCHED])
			state |= STRETCHED;
	}
	return state;
}

#define _SHOD_MOVERESIZE_RELATIVE ((long)(1 << 16))

static void
handle_message(struct Object *self, Atom message, long int data[5])
{
	struct Tab *tab;
	struct Container *container;

	if (self->class == &tab_class) {
		tab = self->self;
		container = tab->row->col->c;
	} else if (self->class == &container_class) {
		container = self->self;
		tab = container->selcol->selrow->seltab;
	} else {
		return;
	}
	if (message == wm.atoms[_NET_WM_STATE]) {
		containersetstate(
			&container->obj,
			atoms2statemask((Atom *)&data[1]),
			data[0]
		);
	} else if (message == wm.atoms[_NET_MOVERESIZE_WINDOW]) {
		XWindowChanges changes = {
			.x	= data[1],
			.y	= data[2],
			.width	= data[3],
			.height	= data[4],
		};

		if (data[0] & _SHOD_MOVERESIZE_RELATIVE) {
			changes.x	+= container->geometry.current.x;
			changes.y	+= container->geometry.current.y;
			changes.width	+= container->geometry.current.width;
			changes.height	+= container->geometry.current.height;
		}
		container_configure(self, CWX | CWY | CWWidth | CWHeight, &changes);
	} else if (message == wm.atoms[_NET_WM_DESKTOP]) {
		containersendtodeskandfocus(container, container->mon, data[0]);
	} else if (message == wm.atoms[_NET_ACTIVE_WINDOW]) {
#define ACTIVECOL(col) if ((col) != NULL) tabfocus(((struct Column *)(col))->selrow->seltab)
#define ACTIVEROW(row) if ((row) != NULL) tabfocus(((struct Row *)(row))->seltab)
		enum relative_focus_direction {
			_SHOD_FOCUS_ABSOLUTE            = 0,
			_SHOD_FOCUS_LEFT_CONTAINER      = 1,
			_SHOD_FOCUS_RIGHT_CONTAINER     = 2,
			_SHOD_FOCUS_TOP_CONTAINER       = 3,
			_SHOD_FOCUS_BOTTOM_CONTAINER    = 4,
			_SHOD_FOCUS_PREVIOUS_CONTAINER  = 5,
			_SHOD_FOCUS_NEXT_CONTAINER      = 6,
			_SHOD_FOCUS_LEFT_WINDOW         = 7,
			_SHOD_FOCUS_RIGHT_WINDOW        = 8,
			_SHOD_FOCUS_TOP_WINDOW          = 9,
			_SHOD_FOCUS_BOTTOM_WINDOW       = 10,
			_SHOD_FOCUS_PREVIOUS_WINDOW     = 11,
			_SHOD_FOCUS_NEXT_WINDOW         = 12,
		};

		switch (data[3]) {
		case _SHOD_FOCUS_LEFT_CONTAINER:
		case _SHOD_FOCUS_RIGHT_CONTAINER:
		case _SHOD_FOCUS_TOP_CONTAINER:
		case _SHOD_FOCUS_BOTTOM_CONTAINER:
			// TODO
			break;
		case _SHOD_FOCUS_PREVIOUS_CONTAINER:
			// TODO
			break;
		case _SHOD_FOCUS_NEXT_CONTAINER:
			// TODO
			break;
		case _SHOD_FOCUS_LEFT_WINDOW:
			ACTIVECOL(TAILQ_PREV(&tab->row->col->obj, Queue, entry));
			break;
		case _SHOD_FOCUS_RIGHT_WINDOW:
			ACTIVECOL(TAILQ_NEXT(&tab->row->col->obj, entry));
			break;
		case _SHOD_FOCUS_TOP_WINDOW:
			ACTIVEROW(TAILQ_PREV(&tab->row->obj, Queue, entry));
			break;
		case _SHOD_FOCUS_BOTTOM_WINDOW:
			ACTIVEROW(TAILQ_NEXT(&tab->row->obj, entry));
			break;
		case _SHOD_FOCUS_PREVIOUS_WINDOW:
			if (TAILQ_PREV(&tab->obj, Queue, entry) != NULL)
				tabfocus((void *)TAILQ_PREV(&tab->obj, Queue, entry));
			else
				tabfocus((void *)TAILQ_LAST(&tab->row->tabq, Queue));
			break;
		case _SHOD_FOCUS_NEXT_WINDOW:
			if (TAILQ_NEXT(&tab->obj, entry) != NULL)
				tabfocus((void *)TAILQ_NEXT(&tab->obj, entry));
			else
				tabfocus((void *)TAILQ_FIRST(&tab->row->tabq));
			break;
		default:
			tabfocus(tab);
			raise(container);
			break;
		}
	} else if (message == wm.atoms[_NET_WM_MOVERESIZE]) {
		/*
		 * Client-side decorated Gtk3 windows emit this signal when being
		 * dragged by their GtkHeaderBar
		 */
		switch (data[2]) {
		case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
			drag_resize(container, BORDER_NW, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOP:
			drag_resize(container, BORDER_N, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
			drag_resize(container, BORDER_NE, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_RIGHT:
			drag_resize(container, BORDER_E, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
			drag_resize(container, BORDER_SE, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
			drag_resize(container, BORDER_S, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
			drag_resize(container, BORDER_SW, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_SIZE_LEFT:
			drag_resize(container, BORDER_W, data[0], data[1]);
			break;
		case _NET_WM_MOVERESIZE_MOVE:
			drag_move(container, data[0], data[1]);
			break;
		default:
			XUngrabPointer(wm.display, CurrentTime);
			break;
		}
	}
}

static void
dialog_message(struct Object *self, Atom message, long int data[5])
{
	struct Dialog *dialog = self->self;

	if (message == wm.atoms[_NET_MOVERESIZE_WINDOW]) {
		XWindowChanges changes = {
			.x	= data[1],
			.y	= data[2],
			.width	= data[3],
			.height	= data[4],
		};

		if (data[0] & _SHOD_MOVERESIZE_RELATIVE) {
			changes.x	+= dialog->x;
			changes.y	+= dialog->y;
			changes.width	+= dialog->maxw;
			changes.height	+= dialog->maxh;
		}
		dialog_configure(self, CWX | CWY | CWWidth | CWHeight, &changes);
	}
}

static void
list_clients(void)
{
	Atom properties[] = {
		wm.atoms[_NET_CLIENT_LIST],
		wm.atoms[_NET_CLIENT_LIST_STACKING],
		wm.atoms[_SHOD_CONTAINER_LIST],
	};
	struct Object *c, *l, *r, *t;
	Window *wins;
	int n;

	if (nclients < 1) {
		/* no client, clear lists */
		for (size_t i = 0; i < LEN(properties); i++) {
			XChangeProperty(
				wm.display, wm.rootwin, properties[i],
				XA_WINDOW, 32, PropModeReplace,
				NULL, 0
			);
		}
		return;
	}

	n = 0;
	wins = ecalloc(nclients, sizeof(*wins));
	TAILQ_FOREACH_REVERSE(c, &wm.stacking_order, Queue, z_entry) {
		struct Container *container;

		if (c->class != &container_class)
			continue;
		container = c->self;
		TAILQ_FOREACH(l, &container->colq, entry) {
			struct Column *col = l->self;
			TAILQ_FOREACH(r, &((struct Column *)l)->rowq, entry)
			TAILQ_FOREACH(t, &((struct Row *)r)->tabq, entry) {
				if (col->selrow->seltab == t->self)
					continue;
				wins[n++] = t->win;
			}
			wins[n++] = col->selrow->seltab->obj.win;
		}
	}
	XChangeProperty(
		wm.display, wm.rootwin, wm.atoms[_NET_CLIENT_LIST_STACKING],
		XA_WINDOW, 32, PropModeReplace,
		(void *)wins, n
	);
	XChangeProperty(
		wm.display, wm.rootwin, wm.atoms[_NET_CLIENT_LIST],
		XA_WINDOW, 32, PropModeReplace,
		(void *)wins, n
	);

	n = 0;
	TAILQ_FOREACH_REVERSE(c, &wm.stacking_order, Queue, z_entry) {
		struct Container *container;

		if (c->class != &container_class)
			continue;
		container = c->self;
		wins[n++] = container->selcol->selrow->seltab->obj.win;
	}
	XChangeProperty(
		wm.display, wm.rootwin, wm.atoms[_SHOD_CONTAINER_LIST],
		XA_WINDOW, 32, PropModeReplace,
		(void *)wins, n
	);
	free(wins);
}

static void
focus(struct Object *obj)
{
	struct Tab *tab;

	if (obj->class == &tab_class) {
		tab = obj->self;
	} else if (obj->class == &container_class) {
		struct Container *container = obj->self;
		tab = container->selcol->selrow->seltab;
	} else if (obj->class == &dialog_class) {
		struct Dialog *dialog = obj->self;
		tab = dialog->tab;
	} else {
		return;
	}
	tabfocus(tab);
}

static struct Container *
alttab_raise(struct Container *prevc, Bool backward)
{
	struct Object *obj;
	struct Container *newc = NULL;

	/*
	 * temporarily raise prev/next container for alttab and return it;
	 * also unhide it if hidden
	 */
	if (prevc == NULL)
		return NULL;
	if (backward) {
		for (obj = &prevc->obj; obj != NULL; obj = TAILQ_PREV(obj, Queue, entry)) {
			if (obj->self != prevc &&
			    is_visible(obj->self, prevc->mon, prevc->desk)) {
				newc = obj->self;
				break;
			}
		}
		if (newc == NULL) TAILQ_FOREACH_REVERSE(obj, &focus_history, Queue, entry) {
			if (obj->self != prevc &&
			    is_visible(obj->self, prevc->mon, prevc->desk)) {
				newc = obj->self;
				break;
			}
		}
	} else {
		for (obj = &prevc->obj; obj != NULL; obj = TAILQ_NEXT(obj, entry)) {
			if (obj->self != prevc &&
			    is_visible(obj->self, prevc->mon, prevc->desk)) {
				newc = obj->self;
				break;
			}
		}
		if (newc == NULL) TAILQ_FOREACH(obj, &focus_history, entry) {
			if (obj->self != prevc &&
			    is_visible(obj->self, prevc->mon, prevc->desk)) {
				newc = obj->self;
				break;
			}
		}
	}
	if (newc == NULL)
		return NULL;

	/*
	 * put back temporarily raised container for alttab;
	 * also hide it if previously hidden
	 */
	if (newc != prevc) {
		container_decorate(prevc, UNFOCUSED);
		restack(&prevc->obj);
		if (prevc->ishidden)
			XUnmapWindow(wm.display, prevc->obj.frame);
		XFlush(wm.display);
	}

	if (newc->ishidden)
		XMapWindow(wm.display, newc->obj.frame);
	XRaiseWindow(wm.display, newc->obj.frame);
	container_decorate(newc, FOCUSED);
	/*
	 * We set client's window as the active window here, even if it
	 * has not been focused yet (it is only focused when calling
	 * tabfocus() later) because compositors may decorate active
	 * windows specially.  So we set it as active temporarily to
	 * be decorated while alt-tab'ing.
	 */
	set_active_window(newc->selcol->selrow->seltab->obj.win);
	return newc;
}

void
alttab(KeyCode altkey, KeyCode tabkey, Bool shift)
{
	struct Container *c;
	XEvent ev;

	if ((c = (void *)TAILQ_FIRST(&focus_history)) == NULL)
		return;
	if (XGrabKeyboard(
		wm.display, wm.rootwin, True, GrabModeAsync, GrabModeAsync,
		CurrentTime
	) != GrabSuccess)
		goto done;
	if (XGrabPointer(
		wm.display, wm.rootwin, False, 0, GrabModeAsync, GrabModeAsync,
		None, None, CurrentTime
	) != GrabSuccess)
		goto done;
	wm.focused = NULL;
	c = alttab_raise(c, shift);
	for (;;) {
		XMaskEvent(wm.display, KeyPressMask|KeyReleaseMask, &ev);
		switch (ev.type) {
		case KeyPress:
			if (ev.xkey.keycode == tabkey && isvalidstate(ev.xkey.state)) {
				c = alttab_raise(c, (ev.xkey.state & ShiftMask));
			}
			break;
		case KeyRelease:
			if (ev.xkey.keycode == altkey)
				goto done;
			break;
		}
	}
done:
	XUngrabKeyboard(wm.display, CurrentTime);
	XUngrabPointer(wm.display, CurrentTime);
	if (c == NULL) {
		tabfocus(NULL);
		return;
	}
	tabfocus(c->selcol->selrow->seltab);
	raise(c);
}

void
focusnext(struct Monitor *mon, int desk)
{
	struct Object *obj;

	TAILQ_FOREACH(obj, &focus_history, entry) {
		struct Container *container = obj->self;
		if (is_visible(container, mon, desk)) {
			tabfocus(container->selcol->selrow->seltab);
			raise(container);
			return;
		}
	}
	tabfocus(NULL);
}

Bool
focused_follows_leader(Window leader)
{
	struct Tab *tab;

	if (wm.focused == NULL)
		return False;
	if (wm.focused->class != &tab_class)
		return False;
	tab = wm.focused->self;
	return (leader == tab->obj.win || leader == tab->leader);
}

struct Class container_class = {
	.focus          = focus,
	.setstate       = containersetstate,
	.manage         = NULL,
	.unmanage       = NULL,
	.btnpress       = container_btnpress,
	.init           = init,
	.clean          = clean,
	.restack        = restack,
	.redecorate     = redecorate,
	.monitor_delete = monitor_delete,
	.monitor_reset  = monitor_reset,
	.redecorate_all = redecorate_all,
	.reload_theme   = reload_theme,
	.show_desktop   = show_desktop,
	.hide_desktop   = hide_desktop,
	.change_desktop  = change_desktop,
	.handle_configure = container_configure,
	.handle_message = handle_message,
	.handle_enter   = handle_enter,
	.list_clients   = list_clients,
};

struct Class tab_class = {
	.focus          = focus,
	.setstate       = containersetstate,
	.manage         = managecontainer,
	.restack        = restack,
	.redecorate     = redecorate,
	.unmanage       = unmanagetab,
	.btnpress       = tab_btnpress,
	.monitor_delete = NULL,
	.handle_property = handle_property,
	.handle_configure = container_configure,
	.handle_message = handle_message,
	.handle_enter   = handle_enter,
};

struct Class dialog_class = {
	.focus          = focus,
	.setstate       = NULL,
	.manage         = managedialog,
	.unmanage       = unmanagedialog,
	.btnpress       = tab_btnpress,
	.monitor_delete = NULL,
	.handle_configure = dialog_configure,
	.handle_message = dialog_message,
	.handle_enter   = handle_enter,
};

struct Class row_class = {
	.setstate       = NULL,
	.manage         = NULL,
	.unmanage       = NULL,
	.btnpress       = rowdiv_btnpress,
	.monitor_delete = NULL,
};

struct Class column_class = {
	.setstate       = NULL,
	.manage         = NULL,
	.unmanage       = NULL,
	.btnpress       = coldiv_btnpress,
	.monitor_delete = NULL,
};
