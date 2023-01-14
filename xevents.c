#include <err.h>

#include "shod.h"

#include <X11/XKBlib.h>

#define MOUSEEVENTMASK          (ButtonReleaseMask | PointerMotionMask | ExposureMask)
#define ALTTABMASK              (KeyPressMask | KeyReleaseMask | ExposureMask)
#define DOUBLECLICK     250     /* time in miliseconds of a double click */
#define BETWEEN(a, b, c)        ((a) <= (b) && (b) < (c))

/* moveresize action */
enum {
	_NET_WM_MOVERESIZE_SIZE_TOPLEFT     = 0,
	_NET_WM_MOVERESIZE_SIZE_TOP         = 1,
	_NET_WM_MOVERESIZE_SIZE_TOPRIGHT    = 2,
	_NET_WM_MOVERESIZE_SIZE_RIGHT       = 3,
	_NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT = 4,
	_NET_WM_MOVERESIZE_SIZE_BOTTOM      = 5,
	_NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT  = 6,
	_NET_WM_MOVERESIZE_SIZE_LEFT        = 7,
	_NET_WM_MOVERESIZE_MOVE             = 8,   /* movement only */
	_NET_WM_MOVERESIZE_SIZE_KEYBOARD    = 9,   /* size via keyboard */
	_NET_WM_MOVERESIZE_MOVE_KEYBOARD    = 10,  /* move via keyboard */
	_NET_WM_MOVERESIZE_CANCEL           = 11,  /* cancel operation */
};

/* focus relative direction */
enum {
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

/* floating object type */
enum {
	FLOAT_CONTAINER,
	FLOAT_MENU,
};

/* motif constants, mostly unused */
enum {
	/*
	 * Constants copied from lib/Xm/MwmUtil.h on motif's source code.
	 */

	PROP_MOTIF_WM_HINTS_ELEMENTS            = 5,
	PROP_MWM_HINTS_ELEMENTS                 = PROP_MOTIF_WM_HINTS_ELEMENTS,

	/* bit definitions for MwmHints.flags */
	MWM_HINTS_FUNCTIONS                     = (1 << 0),
	MWM_HINTS_DECORATIONS                   = (1 << 1),
	MWM_HINTS_INPUT_MODE                    = (1 << 2),
	MWM_HINTS_STATUS                        = (1 << 3),

	/* bit definitions for MwmHints.functions */
	MWM_FUNC_ALL                            = (1 << 0),
	MWM_FUNC_RESIZE                         = (1 << 1),
	MWM_FUNC_MOVE                           = (1 << 2),
	MWM_FUNC_MINIMIZE                       = (1 << 3),
	MWM_FUNC_MAXIMIZE                       = (1 << 4),
	MWM_FUNC_CLOSE                          = (1 << 5),

	/* bit definitions for MwmHints.decorations */
	MWM_DECOR_ALL                           = (1 << 0),
	MWM_DECOR_BORDER                        = (1 << 1),
	MWM_DECOR_RESIZEH                       = (1 << 2),
	MWM_DECOR_TITLE                         = (1 << 3),
	MWM_DECOR_MENU                          = (1 << 4),
	MWM_DECOR_MINIMIZE                      = (1 << 5),
	MWM_DECOR_MAXIMIZE                      = (1 << 6),

	/* values for MwmHints.input_mode */
	MWM_INPUT_MODELESS                      = 0,
	MWM_INPUT_PRIMARY_APPLICATION_MODAL     = 1,
	MWM_INPUT_SYSTEM_MODAL                  = 2,
	MWM_INPUT_FULL_APPLICATION_MODAL        = 3,

	/* bit definitions for MwmHints.status */
	MWM_TEAROFF_WINDOW                      = (1 << 0),
};

/* motif window manager (Mwm) hints */
struct MwmHints {
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
	long          inputMode;
	unsigned long status;
};

void (*managefuncs[TYPE_LAST])(struct Tab *, struct Monitor *, int, Window, Window, XRectangle, int, int) = {
	[TYPE_NOTIFICATION] = managenotif,
	[TYPE_DOCKAPP] = managedockapp,
	[TYPE_NORMAL] = managecontainer,
	[TYPE_DIALOG] = managedialog,
	[TYPE_SPLASH] = managesplash,
	[TYPE_PROMPT] = manageprompt,
	[TYPE_MENU] = managemenu,
	[TYPE_DOCK] = managebar,
};

int (*unmanagefuncs[TYPE_LAST])(struct Object *, int) = {
	[TYPE_NOTIFICATION] = unmanagenotif,
	[TYPE_DOCKAPP] = unmanagedockapp,
	[TYPE_NORMAL] = unmanagecontainer,
	[TYPE_DIALOG] = unmanagedialog,
	[TYPE_SPLASH] = unmanagesplash,
	[TYPE_PROMPT] = unmanageprompt,
	[TYPE_MENU] = unmanagemenu,
	[TYPE_DOCK] = unmanagebar,
};

#define GETMANAGED(head, p, w)                                  \
	TAILQ_FOREACH(p, &(head), entry)                        \
		if (p->win == w)                                \
			return (p);                             \

/* check whether window was placed by the user */
static int
isuserplaced(Window win)
{
	XSizeHints size;
	long dl;

	return (XGetWMNormalHints(dpy, win, &size, &dl) && (size.flags & USPosition));
}

/* check if desktop is visible */
static int
deskisvisible(struct Monitor *mon, int desk)
{
	return mon->seldesk == desk;
}

/* get pointer to managed object given a window */
static struct Object *
getmanaged(Window win)
{
	struct Object *p, *tab, *dial, *menu;
	struct Container *c;
	struct Column *col;
	struct Row *row;
	int i;

	TAILQ_FOREACH(c, &wm.focusq, entry) {
		if (c->frame == win)
			return (struct Object *)c->selcol->selrow->seltab;
		for (i = 0; i < BORDER_LAST; i++)
			if (c->curswin[i] == win)
				return (struct Object *)c->selcol->selrow->seltab;
		TAILQ_FOREACH(col, &(c)->colq, entry) {
			TAILQ_FOREACH(row, &col->rowq, entry) {
				if (row->bar == win || row->bl == win || row->br == win)
					return (struct Object *)row->seltab;
				TAILQ_FOREACH(tab, &row->tabq, entry) {
					if (tab->win == win ||
					    ((struct Tab *)tab)->frame == win ||
					    ((struct Tab *)tab)->title == win)
						return tab;
					TAILQ_FOREACH(dial, &((struct Tab *)tab)->dialq, entry) {
						if (dial->win == win ||
						    ((struct Dialog *)dial)->frame == win)
							return dial;
						}
				}
			}
		}
	}
	GETMANAGED(dock.dappq, p, win)
	GETMANAGED(wm.barq, p, win)
	GETMANAGED(wm.notifq, p, win)
	TAILQ_FOREACH(p, &wm.splashq, entry)
		if (p->win == win || ((struct Splash *)p)->frame == win)
			return p;
	TAILQ_FOREACH(menu, &wm.menuq, entry) {
		if (menu->win == win ||
		    ((struct Menu *)menu)->frame == win ||
		    ((struct Menu *)menu)->button == win ||
		    ((struct Menu *)menu)->titlebar == win)
			return menu;
	}
	return NULL;
}

/* check whether given state matches modifier */
static int
isvalidstate(unsigned int state)
{
	return config.modifier != 0 && (state & config.modifier) == config.modifier;
}

/* check whether given state matches shift */
static int
isshiftstate(unsigned int state)
{
	return config.shift != 0 && (state & config.shift) == config.shift;
}

/* get tab given window is a dialog for */
static struct Tab *
getdialogfor(Window win)
{
	struct Object *obj;
	Window tmpwin;

	if (XGetTransientForHint(dpy, win, &tmpwin))
		if ((obj = getmanaged(tmpwin)) != NULL && obj->type == TYPE_NORMAL)
			return (struct Tab *)obj;
	return NULL;
}

/* get tab equal to leader or having leader as group leader */
static struct Tab *
getleaderof(Window leader)
{
	struct Container *c;
	struct Object *tab;

	TAILQ_FOREACH(c, &wm.focusq, entry) {
		TAB_FOREACH_BEGIN(c, tab){
			if (leader != None && (tab->win == leader || ((struct Tab *)tab)->leader == leader))
				return (struct Tab *)tab;
		}TAB_FOREACH_END
	}
	return NULL;
}

/* get bitmask of container state from given window */
static int
getwinstate(Window win)
{
	int state;
	unsigned long i, nstates;
	unsigned char *list;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */
	Atom *as;

	list = NULL;
	state = 0;
	if (XGetWindowProperty(dpy, win, atoms[_NET_WM_STATE], 0L, 1024, False, XA_ATOM, &da, &di, &nstates, &dl, &list) == Success && list != NULL) {
		as = (Atom *)list;
		for (i = 0; i < nstates; i++) {
			if (as[i] == atoms[_NET_WM_STATE_STICKY]) {
				state |= STICKY;
			} else if (as[i] == atoms[_NET_WM_STATE_MAXIMIZED_VERT]) {
				state |= MAXIMIZED;
			} else if (as[i] == atoms[_NET_WM_STATE_MAXIMIZED_HORZ]) {
				state |= MAXIMIZED;
			} else if (as[i] == atoms[_NET_WM_STATE_HIDDEN]) {
				state |= MINIMIZED;
			} else if (as[i] == atoms[_NET_WM_STATE_SHADED]) {
				state |= SHADED;
			} else if (as[i] == atoms[_NET_WM_STATE_FULLSCREEN]) {
				state |= FULLSCREEN;
			} else if (as[i] == atoms[_NET_WM_STATE_ABOVE]) {
				state |= ABOVE;
			} else if (as[i] == atoms[_NET_WM_STATE_BELOW]) {
				state |= BELOW;
			}
		}
	}
	if (isuserplaced(win))
		state |= USERPLACED;
	XFree(list);
	return state;
}

/* get window's WM_STATE property */
static long
getstate(Window w)
{
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom da;
	int di;

	if (XGetWindowProperty(dpy, w, atoms[WM_STATE], 0L, 2L, False, atoms[WM_STATE],
		&da, &di, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if (n != 0)
		result = *p;
	XFree(p);
	return result;
}

/* get text property from window; return `Success` on success */
static int
gettextprop(Window win, Atom atom, char *buf, size_t size)
{
	XTextProperty tprop;
	int count;
	char **list = NULL;

	if (buf == NULL || size == 0)
		return BadLength;
	buf[0] = '\0';
	if (!XGetTextProperty(dpy, win, &tprop, atom) || tprop.nitems == 0)
		return BadAlloc;
	if (XmbTextPropertyToTextList(dpy, &tprop, &list, &count) != Success ||
	    count < 1 || list == NULL || *list == NULL)
		return BadAlloc;
	strncpy(buf, *list, size - 1);
	buf[size - 1] = '\0';
	XFreeStringList(list);
	XFree(tprop.value);
	return Success;
}

/* get motif window manager hints property from window */
static struct MwmHints *
getmwmhints(Window win)
{
	struct MwmHints *mwmhints;
	unsigned long dl;
	Atom type;
	int di;
	int ret;

	ret = XGetWindowProperty(dpy, win, atoms[_MOTIF_WM_HINTS],
	                         0L, PROP_MWM_HINTS_ELEMENTS,
	                         False, atoms[_MOTIF_WM_HINTS],
	                         &type, &di, &dl, &dl,
	                         (unsigned char **)&mwmhints);
	if ((ret == Success) && (type == atoms[_MOTIF_WM_HINTS]))
		return mwmhints;
	if (mwmhints != NULL)
		XFree(mwmhints);
	return NULL;
}

/* get window info based on its type */
static int
getwintype(Window win, Window *leader, struct Tab **tab, int *state, XRectangle *rect)
{
	/* rules for identifying windows */
	enum { CLASS = 0, INSTANCE = 1, ROLE = 2 };
	char *rule[] = { "_", "_", "_" };

	struct MwmHints *mwmhints;
	XClassHint classh;
	XWMHints *wmhints;
	XrmValue xval;
	Atom prop;
	size_t i;
	long n;
	int type, isdockapp, ismenu, pos;
	char *ds;
	char buf[NAMEMAXLEN];
	char role[NAMEMAXLEN];

	pos = 0;
	*tab = NULL;
	*state = 0;
	type = TYPE_UNKNOWN;
	classh.res_class = NULL;
	classh.res_name = NULL;
	if (gettextprop(win, atoms[WM_WINDOW_ROLE], role, NAMEMAXLEN) == Success)
		rule[ROLE] = role;
	if (XGetClassHint(dpy, win, &classh)) {
		rule[CLASS] = classh.res_class;
		rule[INSTANCE] = classh.res_name;
	}

	/* get window state requested by application */
	*state = getwinstate(win);

	/* get window type (and other info) from default (hardcoded) rules */
	for (i = 0; config.rules[i].class != NULL || config.rules[i].instance != NULL || config.rules[i].role != NULL; i++) {
		if ((config.rules[i].class == NULL    || strcmp(config.rules[i].class, rule[CLASS]) == 0)
		&&  (config.rules[i].instance == NULL || strcmp(config.rules[i].instance, rule[INSTANCE]) == 0)
		&&  (config.rules[i].role == NULL     || strcmp(config.rules[i].role, rule[ROLE]) == 0)) {
			if (config.rules[i].type != TYPE_MENU && config.rules[i].type != TYPE_DIALOG) {
				type = config.rules[i].type;
			}
			if (config.rules[i].state >= 0) {
				*state = config.rules[i].state;
			}
		}
	}

	/* get window type (and other info) from X resources */
	if (xdb != NULL) {
		/* check for window type */
		(void)snprintf(buf, NAMEMAXLEN, "shod.%s.%s.%s.type", rule[CLASS], rule[INSTANCE], rule[ROLE]);
		if (XrmGetResource(xdb, buf, "*", &ds, &xval) == True && xval.addr != NULL) {
			if (strcasecmp(xval.addr, "DESKTOP") == 0) {
				type = TYPE_DESKTOP;
			} else if (strcasecmp(xval.addr, "DOCKAPP") == 0) {
				type = TYPE_DOCKAPP;
			} else if (strcasecmp(xval.addr, "PROMPT") == 0) {
				type = TYPE_PROMPT;
			} else if (strcasecmp(xval.addr, "NORMAL") == 0) {
				type = TYPE_NORMAL;
			}
		}

		/* check for window state */
		(void)snprintf(buf, NAMEMAXLEN, "shod.%s.%s.%s.state", rule[CLASS], rule[INSTANCE], rule[ROLE]);
		if (XrmGetResource(xdb, buf, "*", &ds, &xval) == True && xval.addr != NULL) {
			*state = 0;
			if (strcasestr(xval.addr, "above") != NULL) {
				*state |= ABOVE;
			}
			if (strcasestr(xval.addr, "below") != NULL) {
				*state |= BELOW;
			}
			if (strcasestr(xval.addr, "fullscreen") != NULL) {
				*state |= FULLSCREEN;
			}
			if (strcasestr(xval.addr, "maximized") != NULL) {
				*state |= MAXIMIZED;
			}
			if (strcasestr(xval.addr, "minimized") != NULL) {
				*state |= MINIMIZED;
			}
			if (strcasestr(xval.addr, "shaded") != NULL) {
				*state |= SHADED;
			}
			if (strcasestr(xval.addr, "sticky") != NULL) {
				*state |= STICKY;
			}
			if (strcasestr(xval.addr, "extend") != NULL) {
				*state |= EXTEND;
			}
			if (strcasestr(xval.addr, "shrunk") != NULL) {
				*state |= SHRUNK;
			}
			if (strcasestr(xval.addr, "resized") != NULL) {
				*state |= RESIZED;
			}
		}

		/* check for dockapp position */
		(void)snprintf(buf, NAMEMAXLEN, "shod.%s.%s.%s.dockpos", rule[CLASS], rule[INSTANCE], rule[ROLE]);
		if (XrmGetResource(xdb, buf, "*", &ds, &xval) == True) {
			if ((n = strtol(xval.addr, NULL, 10)) >= 0 && n < INT_MAX) {
				pos = n;
			}
		}
	}

	XFree(classh.res_class);
	XFree(classh.res_name);

	/* we already got the type of the window, return */
	if (type != TYPE_UNKNOWN)
		goto done;

	/* try to guess window type */
	prop = getatomprop(win, atoms[_NET_WM_WINDOW_TYPE]);
	wmhints = XGetWMHints(dpy, win);
	mwmhints = getmwmhints(win);
	ismenu = mwmhints != NULL && (mwmhints->flags & MWM_HINTS_STATUS) && (mwmhints->status & MWM_TEAROFF_WINDOW);
	isdockapp = (wmhints && (wmhints->flags & (IconWindowHint | StateHint)) && wmhints->initial_state == WithdrawnState);
	*leader = getwinprop(win, atoms[WM_CLIENT_LEADER]);
	if (*leader == None)
		*leader = (wmhints != NULL && (wmhints->flags & WindowGroupHint)) ? wmhints->window_group : None;
	*tab = getdialogfor(win);
	XFree(mwmhints);
	XFree(wmhints);
	if (isdockapp) {
		type = TYPE_DOCKAPP;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DESKTOP]) {
		type = TYPE_DESKTOP;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DOCK]) {
		type = TYPE_DOCK;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
		type = TYPE_NOTIFICATION;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_PROMPT]) {
		type = TYPE_PROMPT;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_SPLASH]) {
		type = TYPE_SPLASH;
	} else if (ismenu ||
	           prop == atoms[_NET_WM_WINDOW_TYPE_MENU] ||
	           prop == atoms[_NET_WM_WINDOW_TYPE_UTILITY] ||
	           prop == atoms[_NET_WM_WINDOW_TYPE_TOOLBAR]) {
		if (*tab != NULL)
			*leader = (*tab)->obj.win;
		type = TYPE_MENU;
	} else if (*tab != NULL) {
		if (*tab != NULL)
			*leader = (*tab)->obj.win;
		type = config.floatdialog ? TYPE_MENU : TYPE_DIALOG;
	} else {
		if (prop != atoms[_NET_WM_WINDOW_TYPE_MENU] &&
		    prop != atoms[_NET_WM_WINDOW_TYPE_DIALOG] &&
		    prop != atoms[_NET_WM_WINDOW_TYPE_UTILITY] &&
		    prop != atoms[_NET_WM_WINDOW_TYPE_TOOLBAR]) {
			*tab = getleaderof(*leader);
		}
		type = TYPE_NORMAL;
	}

done:
	if (type == TYPE_DOCKAPP)
		rect->x = rect->y = pos;
	return type;
}

/* select window input events, grab mouse button presses, and clear its border */
static void
preparewin(Window win)
{
	XSelectInput(dpy, win, StructureNotifyMask | PropertyChangeMask | FocusChangeMask);
	XGrabButton(dpy, AnyButton, AnyModifier, win, False, ButtonPressMask,
	            GrabModeSync, GrabModeSync, None, None);
	XSetWindowBorderWidth(dpy, win, 0);
}

/* check whether window is urgent */
static int
getwinurgency(Window win)
{
	XWMHints *wmh;
	int ret;

	ret = 0;
	if ((wmh = XGetWMHints(dpy, win)) != NULL) {
		ret = wmh->flags & XUrgencyHint;
		XFree(wmh);
	}
	return ret;
}

/* get row or column next to division the pointer is on */
static void
getdivisions(struct Container *c, struct Column **cdiv, struct Row **rdiv, int x, int y)
{
	struct Column *col;
	struct Row *row;

	*cdiv = NULL;
	*rdiv = NULL;
	TAILQ_FOREACH(col, &c->colq, entry) {
		if (TAILQ_NEXT(col, entry) != NULL && x >= col->x + col->w && x < col->x + col->w + config.divwidth) {
			*cdiv = col;
			return;
		}
		if (x >= col->x && x < col->x + col->w) {
			TAILQ_FOREACH(row, &col->rowq, entry) {
				if (TAILQ_NEXT(row, entry) != NULL && y >= row->y + row->h && y < row->y + row->h + config.divwidth) {
					*rdiv = row;
					return;
				}
			}
		}
	}
}

/* get frame handle (NW/N/NE/W/E/SW/S/SE) the pointer is on */
static enum Octant
getframehandle(int w, int h, int x, int y)
{
	if ((y < config.borderwidth && x <= config.corner) || (x < config.borderwidth && y <= config.corner))
		return NW;
	else if ((y < config.borderwidth && x >= w - config.corner) || (x > w - config.borderwidth && y <= config.corner))
	      return NE;
	else if ((y > h - config.borderwidth && x <= config.corner) || (x < config.borderwidth && y >= h - config.corner))
	      return SW;
	else if ((y > h - config.borderwidth && x >= w - config.corner) || (x > w - config.borderwidth && y >= h - config.corner))
	      return SE;
	else if (y < config.borderwidth)
	      return N;
	else if (y >= h - config.borderwidth)
	      return S;
	else if (x < config.borderwidth)
	      return W;
	else if (x >= w - config.borderwidth)
	      return E;
	return C;
}

/* get quadrant (NW/NE/SW/SE) the pointer is on */
static enum Octant
getquadrant(int w, int h, int x, int y)
{
	if (x >= w / 2 && y >= h / 2)
		return SE;
	if (x >= w / 2 && y <= h / 2)
		return NE;
	if (x <= w / 2 && y >= h / 2)
		return SW;
	if (x <= w / 2 && y <= h / 2)
		return NW;
	return C;
}

/* (un)show desktop */
static void
deskshow(int show)
{
	struct Object *obj;
	struct Container *c;

	TAILQ_FOREACH(c, &wm.focusq, entry)
		if (!c->isminimized)
			containerhide(c, show);
	TAILQ_FOREACH(obj, &wm.splashq, entry)
		splashhide((struct Splash *)obj, show);
	wm.showingdesk = show;
	ewmhsetshowingdesktop(show);
}

/* update desktop */
void
deskupdate(struct Monitor *mon, int desk)
{
	struct Object *obj;
	struct Splash *splash;
	struct Container *c;

	if (desk < 0 || desk >= config.ndesktops || (mon == wm.selmon && desk == wm.selmon->seldesk))
		return;
	if (wm.showingdesk)
		deskshow(0);
	if (!deskisvisible(mon, desk)) {
		/* unhide cointainers of new current desktop
		 * hide containers of previous current desktop */
		TAILQ_FOREACH(c, &wm.focusq, entry) {
			if (c->mon != mon)
				continue;
			if (!c->isminimized && c->desk == desk) {
				containerhide(c, 0);
			} else if (!c->issticky && c->desk == mon->seldesk) {
				containerhide(c, 1);
			}
		}
		TAILQ_FOREACH(obj, &wm.splashq, entry) {
			splash = (struct Splash *)obj;
			if (splash->mon != mon)
				continue;
			if (splash->desk == desk) {
				splashhide(splash, 0);
			} else if (splash->desk == mon->seldesk) {
				splashhide(splash, 1);
			}
		}
	}
	wm.selmon = mon;
	wm.selmon->seldesk = desk;
	ewmhsetcurrentdesktop(desk);
}

/* change desktop */
static void
deskfocus(struct Monitor *mon, int desk)
{
	struct Container *c;

	if (desk < 0 || desk >= config.ndesktops || (mon == wm.selmon && desk == wm.selmon->seldesk))
		return;
	deskupdate(mon, desk);
	c = getnextfocused(mon, desk);
	if (c != NULL) {
		tabfocus(c->selcol->selrow->seltab, 0);
	} else {
		tabfocus(NULL, 0);
	}
}

/* call one of the manage- functions */
static void
manage(Window win, XRectangle rect, int ignoreunmap)
{
	struct Tab *tab;
	Window leader;
	int state, type;

	if (getmanaged(win) != NULL)
		return;
	type = getwintype(win, &leader, &tab, &state, &rect);
	if (type == TYPE_DESKTOP) {
		/* we do not handle desktop windows */
		XLowerWindow(dpy, win);
		XMapWindow(dpy, win);
		return;
	}
	preparewin(win);
	(*managefuncs[type])(tab, wm.selmon, wm.selmon->seldesk, win, leader, rect, state, ignoreunmap);
}

/* perform container switching (aka alt-tab) */
static void
alttab(XEvent *e)
{
	struct Container *c, *prevfocused;
	XEvent ev;
	int raised;

	prevfocused = wm.focused;
	if ((c = TAILQ_FIRST(&wm.focusq)) == NULL)
		return;
	ev = *e;
	raised = 0;
	if (XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		goto done;
	if (XGrabPointer(dpy, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
		goto done;
	do {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				copypixmap(ev.xexpose.window);
			break;
		case KeyPress:
			if (ev.xkey.keycode == config.tabkeycode && isvalidstate(ev.xkey.state)) {
				containerbacktoplace(c, raised);
				c = containerraisetemp(c, isshiftstate(ev.xkey.state));
				raised = 1;
			} else if (!isvalidstate(ev.xkey.state)) {
				goto done;
			}
			break;
		case KeyRelease:
			if (ev.xkey.keycode == config.altkeycode || !isvalidstate(ev.xkey.state))
				goto done;
			break;
		}
	} while (!XMaskEvent(dpy, ALTTABMASK, &ev));
done:
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	if (c == NULL)
		return;
	if (raised) {
		containerbacktoplace(c, raised);
		wm.focused = prevfocused;
		tabfocus(c->selcol->selrow->seltab, 0);
	}
}

/* detach tab from window with mouse */
static void
mouseretab(struct Tab *tab, int xroot, int yroot, int x, int y)
{
	struct Monitor *mon;
	struct Object *obj;
	struct Row *row;        /* row to be deleted, if emptied */
	struct Container *c;
	Window win;
	XEvent ev;

	row = tab->row;
	c = row->col->c;
	if (XGrabPointer(dpy, root, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
		goto error;
	tabdetach(tab, xroot - x, yroot - y);
	containermoveresize(c, 0);
	XUnmapWindow(dpy, tab->title);
	XMoveWindow(
		dpy, wm.wmcheckwin,
		ev.xmotion.x_root - DNDDIFF - (2 * config.borderwidth + config.titlewidth),
		ev.xmotion.y_root - DNDDIFF - (2 * config.borderwidth + config.titlewidth)
	);
	XRaiseWindow(dpy, wm.wmcheckwin);
	while (!XMaskEvent(dpy, MOUSEEVENTMASK, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				copypixmap(ev.xexpose.window);
			break;
		case MotionNotify:
			XMoveWindow(
				dpy, wm.wmcheckwin,
				ev.xmotion.x_root - DNDDIFF - (2 * config.borderwidth + config.titlewidth),
				ev.xmotion.y_root - DNDDIFF - (2 * config.borderwidth + config.titlewidth)
			);
			break;
		case ButtonRelease:
			goto done;
		}
	}
done:
	XMoveWindow(
		dpy, wm.wmcheckwin,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth)
	);
	xroot = ev.xbutton.x_root - x;
	yroot = ev.xbutton.y_root - y;
	obj = getmanaged(ev.xbutton.subwindow);
	c = NULL;
	if (obj != NULL && obj->type == TYPE_NORMAL && ev.xbutton.subwindow == ((struct Tab *)obj)->row->col->c->frame) {
		c = ((struct Tab *)obj)->row->col->c;
		XTranslateCoordinates(dpy, ev.xbutton.window, c->frame, ev.xbutton.x_root, ev.xbutton.y_root, &x, &y, &win);
	}
	if (row->col->c != c) {
		XUnmapWindow(dpy, tab->frame);
		XReparentWindow(dpy, tab->frame, root, x, y);
	}
	if (!tabattach(c, tab, x, y)) {
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
	ewmhsetactivewindow(tab->obj.win);
error:
	XUngrabPointer(dpy, CurrentTime);
}

/* resize container with mouse */
static void
mouseresize(int type, void *obj, int xroot, int yroot, enum Octant o)
{
	struct Container *c;
	struct Menu *menu;
	Window frame;
	Cursor curs;
	XEvent ev;
	Time lasttime;
	int *nx, *ny, *nw, *nh;
	int x, y, dx, dy;

	if (type == FLOAT_MENU) {
		menu = (struct Menu *)obj;
		nx = &menu->x;
		ny = &menu->y;
		nw = &menu->w;
		nh = &menu->h;
		frame = menu->frame;
		menudecorate(menu, o != C);
	} else {
		c = (struct Container *)obj;
		if (c->isfullscreen || c->b == 0)
			return;
		if (containerisshaded(c)) {
			if (o & W) {
				o = W;
			} else if (o & E) {
				o = E;
			} else {
				return;
			}
		}
		nx = &c->nx;
		ny = &c->ny;
		nw = &c->nw;
		nh = &c->nh;
		frame = c->frame;
		containerdecorate(c, NULL, NULL, 0, o);
	}
	switch (o) {
	case NW:
		curs = wm.cursors[CURSOR_NW];
		break;
	case NE:
		curs = wm.cursors[CURSOR_NE];
		break;
	case SW:
		curs = wm.cursors[CURSOR_SW];
		break;
	case SE:
		curs = wm.cursors[CURSOR_SE];
		break;
	case N:
		curs = wm.cursors[CURSOR_N];
		break;
	case S:
		curs = wm.cursors[CURSOR_S];
		break;
	case W:
		curs = wm.cursors[CURSOR_W];
		break;
	case E:
		curs = wm.cursors[CURSOR_E];
		break;
	default:
		curs = None;
		break;
	}
	if (o & W)
		x = xroot - *nx - config.borderwidth;
	else if (o & E)
		x = *nx + *nw - config.borderwidth - xroot;
	else
		x = 0;
	if (o & N)
		y = yroot - *ny - config.borderwidth;
	else if (o & S)
		y = *ny + *nh - config.borderwidth - yroot;
	else
		y = 0;
	if (XGrabPointer(dpy, frame, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, curs, CurrentTime) != GrabSuccess)
		goto done;
	lasttime = 0;
	while (!XMaskEvent(dpy, MOUSEEVENTMASK, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				copypixmap(ev.xexpose.window);
			break;
		case ButtonRelease:
			goto done;
			break;
		case MotionNotify:
			if (x > *nw)
				x = 0;
			if (y > *nh)
				y = 0;
			if (o & W &&
			    ((ev.xmotion.x_root < xroot && x > ev.xmotion.x_root - *nx) ||
			     (ev.xmotion.x_root > xroot && x < ev.xmotion.x_root - *nx))) {
				dx = xroot - ev.xmotion.x_root;
				if (*nw + dx >= wm.minsize) {
					*nx -= dx;
					*nw += dx;
				}
			} else if (o & E &&
			    ((ev.xmotion.x_root > xroot && x > *nx + *nw - ev.xmotion.x_root) ||
			     (ev.xmotion.x_root < xroot && x < *nx + *nw - ev.xmotion.x_root))) {
				dx = ev.xmotion.x_root - xroot;
				if (*nw + dx >= wm.minsize) {
					*nw += dx;
				}
			}
			if (o & N &&
			    ((ev.xmotion.y_root < yroot && y > ev.xmotion.y_root - *ny) ||
			     (ev.xmotion.y_root > yroot && y < ev.xmotion.y_root - *ny))) {
				dy = yroot - ev.xmotion.y_root;
				if (*nh + dy >= wm.minsize) {
					*ny -= dy;
					*nh += dy;
				}
			} else if (o & S &&
			    ((ev.xmotion.y_root > yroot && *ny + *nh - ev.xmotion.y_root < y) ||
			     (ev.xmotion.y_root < yroot && *ny + *nh - ev.xmotion.y_root > y))) {
				dy = ev.xmotion.y_root - yroot;
				if (*nh + dy >= wm.minsize) {
					*nh += dy;
				}
			}
			if (ev.xmotion.time - lasttime > RESIZETIME) {
				if (type == FLOAT_MENU) {
					menumoveresize(menu);
					menudecorate(menu, 0);
				} else {
					containercalccols(c, 0, 1);
					containermoveresize(c, 0);
					containerredecorate(c, NULL, NULL, o);
				}
				lasttime = ev.xmotion.time;
			}
			xroot = ev.xmotion.x_root;
			yroot = ev.xmotion.y_root;
			break;
		}
	}
done:
	if (type == FLOAT_MENU) {
		menumoveresize(menu);
		menudecorate(menu, 0);
	} else {
		containercalccols(c, 0, 1);
		containermoveresize(c, 1);
		containerdecorate(c, NULL, NULL, 0, 0);
	}
	XUngrabPointer(dpy, CurrentTime);
}

/* move floating entity (container or menu) with mouse */
static void
mousemove(Window win, int type, void *p, int xroot, int yroot, enum Octant o)
{
	struct Container *c;
	struct Menu *menu;
	Window frame;
	XEvent ev;
	Time lasttime;
	int moved, x, y;

	moved = 0;
	if (type == FLOAT_MENU) {
		menu = (struct Menu *)p;
		menudecorate(menu, o);
		frame = menu->frame;
	} else {
		c = (struct Container *)p;
		containerdecorate(c, NULL, NULL, 0, o);
		frame = c->frame;
	}
	x = y = 0;
	lasttime = 0;
	if (win != None)
		XDefineCursor(dpy, win, wm.cursors[CURSOR_MOVE]);
	else if (XGrabPointer(dpy, frame, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, wm.cursors[CURSOR_MOVE], CurrentTime) != GrabSuccess)
		goto done;
	while (!XMaskEvent(dpy, MOUSEEVENTMASK, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				copypixmap(ev.xexpose.window);
			break;
		case ButtonRelease:
			goto done;
			break;
		case MotionNotify:
			moved = 1;
			if (ev.xmotion.time - lasttime > MOVETIME) {
				x = ev.xmotion.x_root - xroot;
				y = ev.xmotion.y_root - yroot;
				if (type == FLOAT_MENU) {
					menuincrmove(menu, x, y);
				} else {
					containerincrmove(c, x, y);
				}
				lasttime = ev.xmotion.time;
				xroot = ev.xmotion.x_root;
				yroot = ev.xmotion.y_root;
			}
			break;
		}
	}
done:
	if (type == FLOAT_MENU) {
		if (!moved)
			menumoveresize(menu);
		menudecorate(menu, 0);
	} else {
		if (!moved)
			containermoveresize(c, 1);
		containerdecorate(c, NULL, NULL, 0, 0);
	}
	if (win != None) {
		XUndefineCursor(dpy, win);
	} else {
		XUngrabPointer(dpy, CurrentTime);
	}
}

/* resize tiles by dragging division with mouse */
static void
mouseretile(struct Container *c, struct Column *cdiv, struct Row *rdiv, int xroot, int yroot)
{
	struct Row *row;
	XEvent ev;
	Cursor curs;
	Time lasttime;
	int x, y;
	int update;

	if (cdiv != NULL && TAILQ_NEXT(cdiv, entry) != NULL)
		curs = wm.cursors[CURSOR_H];
	else if (rdiv != NULL && TAILQ_NEXT(rdiv, entry) != NULL)
		curs = wm.cursors[CURSOR_V];
	else
		return;
	x = y = 0;
	update = 0;
	lasttime = 0;
	containerdecorate(c, cdiv, rdiv, 0, 0);
	if (XGrabPointer(dpy, c->frame, False, ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, curs, CurrentTime) != GrabSuccess)
		goto done;
	while (!XMaskEvent(dpy, MOUSEEVENTMASK, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				copypixmap(ev.xexpose.window);
			break;
		case ButtonRelease:
			goto done;
			break;
		case MotionNotify:
			x = ev.xmotion.x_root - xroot;
			y = ev.xmotion.y_root - yroot;
			if (cdiv != NULL) {
				if (x < 0 && cdiv->w + x >= wm.minsize) {
					cdiv->w += x;
					TAILQ_NEXT(cdiv, entry)->w -= x;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				} else if (x > 0 && TAILQ_NEXT(cdiv, entry)->w - x >= wm.minsize) {
					TAILQ_NEXT(cdiv, entry)->w -= x;
					cdiv->w += x;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
			}
			for (row = rdiv; row != NULL && TAILQ_NEXT(row, entry) != NULL; ) {
				if (y < 0) {
					if (row->h + y < config.titlewidth) {
						row = TAILQ_PREV(row, RowQueue, entry);
						ev.xmotion.y_root -= y;
						continue;
					}
					row->h += y;
					TAILQ_NEXT(row, entry)->h -= y;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
				if (y > 0) {
					if (TAILQ_NEXT(row, entry)->h - y < config.titlewidth) {
						row = TAILQ_NEXT(row, entry);
						ev.xmotion.y_root -= y;
						continue;
					}
					TAILQ_NEXT(row, entry)->h -= y;
					row->h += y;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
				break;
			}
			if (update) {
				containercalccols(c, 1, 1);
				containermoveresize(c, 0);
				containerdecorate(c, cdiv, rdiv, 0, 0);
				lasttime = ev.xmotion.time;
				update = 0;
			}
			xroot = ev.xmotion.x_root;
			yroot = ev.xmotion.y_root;
			break;
		}
	}
done:
	containercalccols(c, 1, 1);
	containermoveresize(c, 0);
	tabfocus(c->selcol->selrow->seltab, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* handle mouse operation, focusing and raising */
static void
xeventbuttonpress(XEvent *e)
{
	static struct Container *lastc = NULL;
	static Time lasttime = 0;
	struct Object *obj;
	struct Monitor *mon;
	struct Container *c;
	struct Column *cdiv;
	struct Row *rdiv;
	struct Tab *tab;
	struct Menu *menu;
	enum Octant o;
	XButtonPressedEvent *ev;
	Window dw;
	int x, y;

	ev = &e->xbutton;

	if ((obj = getmanaged(ev->window)) == NULL) {
		/*
		 * If user clicked in no managed window, focus the
		 * monitor below the cursor, but only if the click
		 * occurred inside monitor's window area.
		 */
		if ((mon = getmon(ev->x_root, ev->y_root)) != NULL &&
		    ev->x_root >= mon->wx && ev->x_root < mon->wx + mon->ww &&
		    ev->y_root >= mon->wy && ev->y_root < mon->wy + mon->wh) {
			deskfocus(mon, mon->seldesk);
		}
		goto done;
	}

	menu = NULL;
	tab = NULL;
	c = NULL;
	switch (obj->type) {
	case TYPE_NORMAL:
		tab = (struct Tab *)obj;
		c = tab->row->col->c;
		break;
	case TYPE_DIALOG:
		tab = ((struct Dialog *)obj)->tab;
		c = tab->row->col->c;
		break;
	case TYPE_MENU:
		menu = (struct Menu *)obj;
		break;
	case TYPE_SPLASH:
		splashrise((struct Splash *)obj);
		goto done;
	default:
		if ((mon = getmon(ev->x_root, ev->y_root)) != NULL)
			deskfocus(mon, mon->seldesk);
		goto done;
	}

	/* raise menu above others or focus tab */
	if (ev->button == Button1) {
		if (menu != NULL) {
			menufocusraise(menu);
		} else {
			tabfocus(tab, 1);
		}
	}

	/* raise client */
	if (ev->button == Button1) {
		if (c != NULL) {
			containerraise(c, c->isfullscreen, c->abovebelow);
		} else if (menu != NULL) {
			menuraise(menu);
		}
	}

	/* do action performed by mouse */
	if (menu != NULL) {
		if (ev->window == menu->titlebar && ev->button == Button1) {
			mousemove(menu->titlebar, FLOAT_MENU, menu, ev->x_root, ev->y_root, 1);
		} else if (ev->window == menu->button && ev->button == Button1) {
			buttonrightdecorate(menu->button, menu->pixbutton, FOCUSED, 1);
		} else if (isvalidstate(ev->state) && ev->button == Button1) {
			mousemove(None, FLOAT_MENU, menu, ev->x_root, ev->y_root, 0);
		} else if (ev->window == menu->frame && ev->button == Button3) {
			mousemove(None, FLOAT_MENU, menu, ev->x_root, ev->y_root, 0);
		} else if (isvalidstate(ev->state) && ev->button == Button3) {
			if (!XTranslateCoordinates(dpy, ev->window, menu->frame, ev->x, ev->y, &x, &y, &dw))
				goto done;
			o = getquadrant(menu->w, menu->h, x, y);
			mouseresize(FLOAT_MENU, menu, ev->x_root, ev->y_root, o);
		}
	} else if (tab != NULL) {
		if (!XTranslateCoordinates(dpy, ev->window, c->frame, ev->x, ev->y, &x, &y, &dw))
			goto done;
		if (ev->window == tab->title && ev->button == Button1) {
			if (lastc == c && ev->time - lasttime < DOUBLECLICK) {
				containersetstate(
					tab,
					(Atom []){
						atoms[_NET_WM_STATE_MAXIMIZED_VERT],
						atoms[_NET_WM_STATE_MAXIMIZED_HORZ],
					},
					TOGGLE
				);
				lasttime = 0;
				lastc = NULL;
				goto done;
			}
			lastc = c;
			lasttime = ev->time;
		}
		o = getframehandle(c->w, c->h, x, y);
		if (ev->window == tab->title && ev->button == Button3) {
			mouseretab(tab, ev->x_root, ev->y_root, ev->x, ev->y);
		} else if (ev->window == tab->row->bl && ev->button == Button1) {
			wm.presswin = ev->window;
			buttonleftdecorate(tab->row->bl, tab->row->pixbl, FOCUSED, 1);
		} else if (ev->window == tab->row->br && ev->button == Button1) {
			wm.presswin = ev->window;
			buttonrightdecorate(tab->row->br, tab->row->pixbr, FOCUSED, 1);
		} else if (ev->window == c->frame && ev->button == Button1 && o == C) {
			getdivisions(c, &cdiv, &rdiv, x, y);
			if (cdiv != NULL || rdiv != NULL) {
				mouseretile(c, cdiv, rdiv, ev->x_root, ev->y_root);
			}
		} else if (!c->isfullscreen && !c->isminimized && !c->ismaximized) {
			if (isvalidstate(ev->state) && ev->button == Button1) {
				mousemove(None, FLOAT_CONTAINER, c, ev->x_root, ev->y_root, 0);
			} else if (ev->window == c->frame && ev->button == Button3) {
				mousemove(None, FLOAT_CONTAINER, c, ev->x_root, ev->y_root, o);
			} else if ((isvalidstate(ev->state) && ev->button == Button3) ||
		           	   (o != C && ev->window == c->frame && ev->button == Button1)) {
				if (o == C)
					o = getquadrant(c->w, c->h, x, y);
				mouseresize(FLOAT_CONTAINER, c, ev->x_root, ev->y_root, o);
			} else if (ev->window == tab->title && ev->button == Button1) {
				tabdecorate(tab, 1);
				mousemove(tab->title, FLOAT_CONTAINER, c, ev->x_root, ev->y_root, 0);
				tabdecorate(tab, 0);
			}
		}
	}

done:
	XAllowEvents(dpy, ReplayPointer, CurrentTime);
}

/* handle click on titlebar button */
static void
xeventbuttonrelease(XEvent *e)
{
	struct Object *obj;
	struct Row *row;
	struct Menu *menu;
	XButtonReleasedEvent *ev;
	Window win, button;
	Pixmap pix;
	int perform;
	enum { PRESS_CLOSE, PRESS_STACK } action;

	ev = &e->xbutton;
	wm.presswin = None;
	if (ev->button != Button1)
		return;
	if ((obj = getmanaged(ev->window)) == NULL)
		return;
	switch (obj->type) {
	case TYPE_NORMAL:
		row = ((struct Tab *)obj)->row;
		if (ev->window == row->br) {
			action = PRESS_CLOSE;
			button = row->br;
			pix = row->pixbr;
		} else if (ev->window == row->bl) {
			action = PRESS_STACK;
			button = row->bl;
			pix = row->pixbl;
		} else {
			return;
		}
		win = TAILQ_EMPTY(&row->seltab->dialq)
		    ? row->seltab->obj.win
		    : TAILQ_FIRST(&row->seltab->dialq)->win;
		break;
	case TYPE_MENU:
		menu = (struct Menu *)obj;
		if (ev->window == menu->button) {
			action = PRESS_CLOSE;
			button = menu->button;
			win = menu->obj.win;
			pix = menu->pixbutton;
		} else {
			return;
		}
		break;
	default:
		return;
	}
	perform = BETWEEN(0, ev->x, config.titlewidth) &&
	          BETWEEN(0, ev->y, config.titlewidth);
	switch (action) {
	case PRESS_CLOSE:
		if (perform)
			winclose(win);
		buttonrightdecorate(button, pix, FOCUSED, 0);
		break;
	case PRESS_STACK:
		rowstack(row->col, row);
		tabfocus(row->seltab, 0);
		buttonleftdecorate(button, pix, FOCUSED, 0);
		break;
	}
}

/* handle client message event */
static void
xeventclientmessage(XEvent *e)
{
	struct Container *c = NULL;
	struct Tab *tab = NULL;
	struct Object *obj;
	XClientMessageEvent *ev;
	XWindowChanges wc;
	unsigned value_mask = 0;
	int floattype;
	void *p;

	ev = &e->xclient;
	if ((obj = getmanaged(ev->window)) != NULL) {
		switch (obj->type) {
		case TYPE_NORMAL:
			tab = (struct Tab *)obj;
			c = tab->row->col->c;
			break;
		case TYPE_DIALOG:
			tab = ((struct Dialog *)obj)->tab;
			c = tab->row->col->c;
			break;
		}
	}
	if (ev->message_type == atoms[_NET_CURRENT_DESKTOP]) {
		deskfocus(wm.selmon, ev->data.l[0]);
	} else if (ev->message_type == atoms[_NET_SHOWING_DESKTOP]) {
		if (ev->data.l[0]) {
			deskshow(1);
		} else {
			deskfocus(wm.selmon, wm.selmon->seldesk);
		}
	} else if (ev->message_type == atoms[_NET_WM_STATE]) {
		if (obj == NULL || obj->type != TYPE_NORMAL)
			return;
		containersetstate(tab, (Atom *)(ev->data.l + 1), ev->data.l[0]);
	} else if (ev->message_type == atoms[_NET_ACTIVE_WINDOW]) {
#define ACTIVATECOL(col) { if ((col) != NULL) tabfocus((col)->selrow->seltab, 1); }
#define ACTIVATEROW(row) { if ((row) != NULL) tabfocus((row)->seltab, 1); }
		if (tab == NULL && wm.focused != NULL) {
			c = wm.focused;
			tab = wm.focused->selcol->selrow->seltab;
		}
		if (tab == NULL)
			return;
		switch (ev->data.l[3]) {
		case _SHOD_FOCUS_LEFT_CONTAINER:
		case _SHOD_FOCUS_RIGHT_CONTAINER:
		case _SHOD_FOCUS_TOP_CONTAINER:
		case _SHOD_FOCUS_BOTTOM_CONTAINER:
			// removed
			break;
		case _SHOD_FOCUS_PREVIOUS_CONTAINER:
			// removed
			break;
		case _SHOD_FOCUS_NEXT_CONTAINER:
			// removed
			break;
		case _SHOD_FOCUS_LEFT_WINDOW:
			ACTIVATECOL(TAILQ_PREV(tab->row->col, ColumnQueue, entry))
			break;
		case _SHOD_FOCUS_RIGHT_WINDOW:
			ACTIVATECOL(TAILQ_NEXT(tab->row->col, entry))
			break;
		case _SHOD_FOCUS_TOP_WINDOW:
			ACTIVATEROW(TAILQ_PREV(tab->row, RowQueue, entry))
			break;
		case _SHOD_FOCUS_BOTTOM_WINDOW:
			ACTIVATEROW(TAILQ_NEXT(tab->row, entry))
			break;
		case _SHOD_FOCUS_PREVIOUS_WINDOW:
			obj = (struct Object *)tab;
			if (TAILQ_PREV(obj, Queue, entry) != NULL)
				tabfocus((struct Tab *)TAILQ_PREV(obj, Queue, entry), 1);
			else
				tabfocus((struct Tab *)TAILQ_LAST(&tab->row->tabq, Queue), 1);
			break;
		case _SHOD_FOCUS_NEXT_WINDOW:
			obj = (struct Object *)tab;
			if (TAILQ_NEXT(obj, entry) != NULL)
				tabfocus((struct Tab *)TAILQ_NEXT(obj, entry), 1);
			else
				tabfocus((struct Tab *)TAILQ_FIRST(&tab->row->tabq), 1);
			break;
		default:
			tabfocus(tab, 1);
			break;
		}
	} else if (ev->message_type == atoms[_NET_CLOSE_WINDOW]) {
		winclose(ev->window);
	} else if (ev->message_type == atoms[_NET_MOVERESIZE_WINDOW]) {
		if (c == NULL)
			return;
		value_mask = CWX | CWY | CWWidth | CWHeight;
		wc.x = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? c->x + ev->data.l[1] : ev->data.l[1];
		wc.y = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? c->y + ev->data.l[2] : ev->data.l[2];
		wc.width = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? c->w + ev->data.l[3] : ev->data.l[3];
		wc.height = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? c->h + ev->data.l[4] : ev->data.l[4];
		if (obj->type == TYPE_DIALOG) {
			dialogconfigure((struct Dialog *)obj, value_mask, &wc);
		} else if (obj->type == TYPE_NORMAL) {
			containerconfigure(c, value_mask, &wc);
		}
	} else if (ev->message_type == atoms[_NET_WM_DESKTOP]) {
		if (obj == NULL || obj->type != TYPE_NORMAL)
			return;
		containersendtodeskandfocus(c, c->mon, ev->data.l[0]);
	} else if (ev->message_type == atoms[_NET_REQUEST_FRAME_EXTENTS]) {
		if (c == NULL) {
			/*
			 * A client can request an estimate for the frame size
			 * which the window manager will put around it before
			 * actually mapping its window. Java does this (as of
			 * openjdk-7).
			 */
			ewmhsetframeextents(ev->window, config.borderwidth, config.titlewidth);
		} else {
			ewmhsetframeextents(ev->window, c->b, (obj->type == TYPE_DIALOG ? 0 : TITLEWIDTH(c)));
		}
	} else if (ev->message_type == atoms[_NET_WM_MOVERESIZE]) {
		/*
		 * Client-side decorated Gtk3 windows emit this signal when being
		 * dragged by their GtkHeaderBar
		 */
		if (obj == NULL || (obj->type != TYPE_NORMAL && obj->type != TYPE_MENU))
			return;
		if (obj->type == TYPE_MENU) {
			p = obj;
			floattype = FLOAT_MENU;
		} else {
			p = c;
			floattype = FLOAT_CONTAINER;
		}
		switch (ev->data.l[2]) {
		case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], NW);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOP:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], N);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], NE);
			break;
		case _NET_WM_MOVERESIZE_SIZE_RIGHT:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], E);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], SE);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], S);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], SW);
			break;
		case _NET_WM_MOVERESIZE_SIZE_LEFT:
			mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], W);
			break;
		case _NET_WM_MOVERESIZE_MOVE:
			mousemove(None, floattype, p, ev->data.l[0], ev->data.l[1], C);
			break;
		default:
			XUngrabPointer(dpy, CurrentTime);
			break;
		}
	}
}

/* handle configure request event */
static void
xeventconfigurerequest(XEvent *e)
{
	XConfigureRequestEvent *ev;
	XWindowChanges wc;
	struct Object *obj;

	ev = &e->xconfigurerequest;
	wc.x = ev->x;
	wc.y = ev->y;
	wc.width = ev->width;
	wc.height = ev->height;
	wc.border_width = ev->border_width;
	wc.sibling = ev->above;
	wc.stack_mode = ev->detail;
	obj = getmanaged(ev->window);
	if (obj == NULL) {
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	} else if (obj->type == TYPE_DIALOG) {
		dialogconfigure((struct Dialog *)obj, ev->value_mask, &wc);
	} else if (obj->type == TYPE_MENU) {
		menuconfigure((struct Menu *)obj, ev->value_mask, &wc);
	} else if (obj->type == TYPE_DOCKAPP) {
		dockappconfigure((struct Dockapp *)obj, ev->value_mask, &wc);
	} else if (obj->type == TYPE_NORMAL) {
		if (config.honorconfig) {
			containerconfigure(((struct Tab *)obj)->row->col->c, ev->value_mask, &wc);
		} else {
			containermoveresize(((struct Tab *)obj)->row->col->c, 1);
		}
	}
}

/* forget about client */
static void
xeventdestroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev;
	struct Object *obj;

	ev = &e->xdestroywindow;
	if ((obj = getmanaged(ev->window)) != NULL) {
		if (obj->win == ev->window && (*unmanagefuncs[obj->type])(obj, 0)) {
			wm.setclientlist = 1;
		}
	}
}

/* focus window when cursor enter it (only if there is no focus button) */
static void
xevententernotify(XEvent *e)
{
	struct Object *obj;

	if (!config.sloppyfocus)
		return;
	while (XCheckTypedEvent(dpy, EnterNotify, e))
		;
	if ((obj = getmanaged(e->xcrossing.window)) == NULL)
		return;
	switch (obj->type) {
	case TYPE_MENU:
		menufocus((struct Menu *)obj);
		break;
	case TYPE_DIALOG:
		tabfocus(((struct Dialog *)obj)->tab, 1);
		break;
	case TYPE_NORMAL:
		tabfocus((struct Tab *)obj, 1);
		break;
	}
}

/* redraw window decoration */
static void
xeventexpose(XEvent *e)
{
	XExposeEvent *ev;

	ev = &e->xexpose;
	if (ev->count == 0) {
		copypixmap(ev->window);
	}
}

/* handle focusin event */
static void
xeventfocusin(XEvent *e)
{
	XFocusChangeEvent *ev;
	struct Object *obj;

	ev = &e->xfocus;
	if (wm.focused == NULL) {
		tabfocus(NULL, 0);
		return;
	}
	obj = getmanaged(ev->window);
	if (obj == NULL)
		goto focus;
	switch (obj->type) {
	case TYPE_MENU:
		menufocus((struct Menu *)obj);
		break;
	case TYPE_DIALOG:
		if (((struct Dialog *)obj)->tab != wm.focused->selcol->selrow->seltab)
			goto focus;
		break;
	case TYPE_NORMAL:
		if ((struct Tab *)obj != wm.focused->selcol->selrow->seltab)
			goto focus;
		break;
	}
	return;
focus:
	tabfocus(wm.focused->selcol->selrow->seltab, 1);
}

/* key press event on focuswin */
static void
xeventkeypress(XEvent *e)
{
	XKeyPressedEvent *ev;

	ev = &e->xkey;
	if (!config.disablealttab && ev->keycode == config.tabkeycode) {
		alttab(e);
	}
	if (ev->window == wm.wmcheckwin) {
		e->xkey.window = root;
		XSendEvent(dpy, root, False, KeyPressMask, e);
	}
}

/* manage window */
static void
xeventmaprequest(XEvent *e)
{
	XMapRequestEvent *ev;
	XWindowAttributes wa;

	ev = &e->xmaprequest;
	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	manage(
		ev->window,
		(XRectangle){
			.x = wa.x,
			.y = wa.y,
			.width = wa.width,
			.height = wa.height,
		},
		0
	);
}

/* forget about client */
static void
xeventmappingnotify(XEvent *e)
{
	(void)e;
	setmod();
}

/* update client properties */
static void
xeventpropertynotify(XEvent *e)
{
	XPropertyEvent *ev;
	XTextProperty prop;
	struct Container *c;
	struct Object *obj;
	struct Tab *tab;
	struct Menu *menu;

	ev = &e->xproperty;
	if (ev->window == root && ev->atom == XA_RESOURCE_MANAGER) {
		if (!XGetTextProperty(dpy, root, &prop, XA_RESOURCE_MANAGER))
			return;
		XrmDestroyDatabase(xdb);
		setresources(prop.value);
		XFree(prop.value);
		cleantheme();
		inittheme();
		TAILQ_FOREACH(c, &wm.focusq, entry)
			containerdecorate(c, NULL, NULL, 1, C);
		TAILQ_FOREACH(obj, &wm.menuq, entry)
			menudecorate((struct Menu *)obj, 0);
		TAILQ_FOREACH(obj, &wm.notifq, entry)
			notifdecorate((struct Notification *)obj);
		dockdecorate();
		return;
	}
	if (ev->state == PropertyDelete)
		return;
	obj = getmanaged(ev->window);
	if (obj == NULL)
		return;
	if (obj->type == TYPE_NORMAL && ev->window == obj->win) {
		tab = (struct Tab *)obj;
		if (ev->atom == XA_WM_NAME || ev->atom == atoms[_NET_WM_NAME]) {
			winupdatetitle(tab->obj.win, &tab->name);
			tabdecorate(tab, 0);
		} else if (ev->atom == XA_WM_HINTS) {
			tabupdateurgency(tab, getwinurgency(tab->obj.win));
		}
	} else if (obj->type == TYPE_DOCK && (ev->atom == _NET_WM_STRUT_PARTIAL || ev->atom == _NET_WM_STRUT)) {
		barstrut((struct Bar *)obj);
		monupdatearea();
	} else if (obj->type == TYPE_MENU && ev->window == obj->win) {
		menu = (struct Menu *)obj;
		if (ev->atom == XA_WM_NAME || ev->atom == atoms[_NET_WM_NAME]) {
			winupdatetitle(menu->obj.win, &menu->name);
			menudecorate(menu, 0);
		}
	}
}

/* forget about client */
static void
xeventunmapnotify(XEvent *e)
{
	XUnmapEvent *ev;
	struct Object *obj;

	ev = &e->xunmap;
	if ((obj = getmanaged(ev->window)) != NULL) {
		if (obj->win == ev->window && (*unmanagefuncs[obj->type])(obj, 1)) {
			wm.setclientlist = 1;
		}
	}
}

/* scan for already existing windows and adopt them */
void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, transwin, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState) {
				manage(
					wins[i],
					(XRectangle){
						.x = wa.x,
						.y = wa.y,
						.width = wa.width,
						.height = wa.height,
					},
					IGNOREUNMAP
				);
			}
		}
		for (i = 0; i < num; i++) {     /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &transwin) &&
			   (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)) {
				manage(
					wins[i],
					(XRectangle){
						.x = wa.x,
						.y = wa.y,
						.width = wa.width,
						.height = wa.height,
					},
					IGNOREUNMAP
				);
			}
		}
		if (wins != NULL) {
			XFree(wins);
		}
	}
}

/* set modifier and Alt key code from given key sym */
void
setmod(void)
{
	config.altkeycode = 0;
	config.modifier = 0;
	if ((config.altkeycode = XKeysymToKeycode(dpy, config.altkeysym)) == 0) {
		warnx("could not get keycode from keysym");
		return;
	}
	if ((config.tabkeycode = XKeysymToKeycode(dpy, config.tabkeysym)) == 0) {
		warnx("could not get keycode from keysym");
		return;
	}
	if ((config.modifier = XkbKeysymToModifiers(dpy, config.altkeysym)) == 0) {
		warnx("could not get modifier from keysym");
		return;
	}
	if (config.disablealttab)
		return;
	XUngrabKey(dpy, config.tabkeycode, config.modifier, root);
	XUngrabKey(dpy, config.tabkeycode, config.modifier | config.shift, root);
	XGrabKey(dpy, config.tabkeycode, config.modifier, root, False, GrabModeAsync, GrabModeAsync);
	XGrabKey(dpy, config.tabkeycode, config.modifier | config.shift, root, False, GrabModeAsync, GrabModeAsync);
}

void (*xevents[LASTEvent])(XEvent *) = {
	[ButtonPress]      = xeventbuttonpress,
	[ButtonRelease]    = xeventbuttonrelease,
	[ClientMessage]    = xeventclientmessage,
	[ConfigureRequest] = xeventconfigurerequest,
	[DestroyNotify]    = xeventdestroynotify,
	[EnterNotify]      = xevententernotify,
	[Expose]           = xeventexpose,
	[FocusIn]          = xeventfocusin,
	[KeyPress]         = xeventkeypress,
	[MapRequest]       = xeventmaprequest,
	[MappingNotify]    = xeventmappingnotify,
	[PropertyNotify]   = xeventpropertynotify,
	[UnmapNotify]      = xeventunmapnotify,
};
