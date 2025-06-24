#include <err.h>
#include <spawn.h>

#include "shod.h"

#include <X11/XKBlib.h>

#define _SHOD_MOVERESIZE_RELATIVE       ((long)(1 << 16))
#define MOUSEEVENTMASK          (ButtonReleaseMask | PointerMotionMask)
#define BETWEEN(a, b, c)        ((a) <= (b) && (b) < (c))

/* call object method, if it exists */
#define ARG1(arg, ...) (arg)
#define MESSAGE(method, ...) \
	if (ARG1(__VA_ARGS__, 0)->class->method != NULL) \
		ARG1(__VA_ARGS__, 0)->class->method(__VA_ARGS__)
#define unmanage(...) MESSAGE(unmanage, __VA_ARGS__)
#define btnpress(...) MESSAGE(btnpress, __VA_ARGS__)

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

	PROP_MWM_HINTS_ELEMENTS                 = 5,

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

/* GNUstep constants, mostly unused */
enum {
	/*
	 * Constants copied from src/GNUstep.h on window-maker's source code.
	 */

	PROP_GNU_HINTS_ELEMENTS                 = 9,

	/* flags */
	GNU_FLAG_WINDOWSTYLE    = (1<<0),
	GNU_FLAG_WINDOWLEVEL    = (1<<1),

	/* window levels */
	GNU_LEVEL_DESKTOP       = -1000,
	GNU_LEVEL_NORMAL        = 0,
	GNU_LEVEL_FLOATING      = 3,
	GNU_LEVEL_SUBMENU       = 3,
	GNU_LEVEL_TORNOFF       = 3,
	GNU_LEVEL_MAINMENU      = 20,
	GNU_LEVEL_DOCK          = 21,
	GNU_LEVEL_STATUS        = 21,
	GNU_LEVEL_PANEL         = 100,
	GNU_LEVEL_POPUP         = 101,
	GNU_LEVEL_SCREENSAVER   = 1000,

	/* window style */
	GNU_STYLE_TITLED        = 1,
	GNU_STYLE_CLOSABLE      = 2,
	GNU_STYLE_MINIMIZABLE   = 4,
	GNU_STYLE_RESIZEABLE    = 8,
	GNU_STYLE_ICON          = 64,
	GNU_STYLE_MINIWINDOW    = 128,
};

/* motif window manager (Mwm) hints */
struct MwmHints {
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
	long          inputMode;
	unsigned long status;
};

/* GNUstep window manager hints */
struct GNUHints {
	unsigned long flags;
	unsigned long window_style;
	unsigned long window_level;
	unsigned long reserved;
	unsigned long miniaturize_pixmap;       /* pixmap for miniaturize button */
	unsigned long close_pixmap;             /* pixmap for close button */
	unsigned long miniaturize_mask;         /* miniaturize pixmap mask */
	unsigned long close_mask;               /* close pixmap mask */
	unsigned long extra_flags;
};

#define GETMANAGED(head, p, w)                                  \
	TAILQ_FOREACH(p, &(head), entry)                        \
		if (p->win == w)                                \
			return (p);                             \

static void manageunknown(struct Tab *, struct Monitor *, int, Window,
		Window, XRectangle, enum State);

static struct Class *unknown_class = &(struct Class){
	.type           = TYPE_UNKNOWN,
	.setstate       = NULL,
	.manage         = manageunknown,
	.unmanage       = NULL,
};

static KeyCode altkey;
static KeyCode tabkey;

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

struct Object *
getmanaged(Window win)
{
	struct Object *p, *r, *tab, *col, *dial, *menu;
	struct Container *c;
	struct Row *row;

	if ((p = context_get(win)) != NULL)
		return p;
#warning TODO: add classes for frames/decorations, then remove getmanaged()
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		TAILQ_FOREACH(col, &(c)->colq, entry) {
			TAILQ_FOREACH(r, &((struct Column *)col)->rowq, entry) {
				row = (struct Row *)r;
				if (row->bar == win || row->bl == win || row->br == win)
					return (struct Object *)row->seltab;
				TAILQ_FOREACH(tab, &row->tabq, entry) {
					TAILQ_FOREACH(dial, &((struct Tab *)tab)->dialq, entry) {
						}
				}
			}
		}
	}
	TAILQ_FOREACH(menu, &wm.menuq, entry) {
		if (((struct Menu *)menu)->frame == win ||
		    ((struct Menu *)menu)->button == win ||
		    ((struct Menu *)menu)->titlebar == win)
			return menu;
	}
	return NULL;
}

/* check whether given state matches modifier */
Bool
isvalidstate(unsigned int state)
{
	return config.modifier != 0 && (state & config.modifier) == config.modifier;
}

/* get tab given window is a dialog for */
static struct Tab *
getdialogfor(Window win)
{
	struct Object *obj;
	Window tmpwin;

	if (XGetTransientForHint(dpy, win, &tmpwin)) {
		obj = getmanaged(tmpwin);
		if (obj == NULL)
			return NULL;
		if (obj->class->type != TYPE_NORMAL)
			return NULL;
		return (struct Tab *)obj;
	}
	return NULL;
}

static struct Tab *
gettabfrompid(unsigned long pid)
{
	struct Container *c;
	struct Object *tab;

	if (pid <= 1)
		return NULL;
	TAILQ_FOREACH(c, &wm.focusq, entry) {
		TAB_FOREACH_BEGIN(c, tab){
			if (pid == ((struct Tab *)tab)->pid)
				return (struct Tab *)tab;
		}TAB_FOREACH_END
	}
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
static enum State
getwinstate(Window win)
{
	enum State state;
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

static char *
gettextprop(Window win, Atom atom)
{
	XTextProperty tprop = { .value = NULL };
	int count;
	char **list = NULL;
	char *s = NULL;

	if (!XGetTextProperty(dpy, win, &tprop, atom))
		goto error;
	if (tprop.nitems == 0)
		goto error;
	if (XmbTextPropertyToTextList(dpy, &tprop, &list, &count) != Success)
		goto error;
	if (count < 1 || list == NULL || *list == NULL)
		goto error;
	s = strdup(list[0]);
error:
	XFreeStringList(list);
	XFree(tprop.value);
	return s;
}

/* get motif/GNUstep hints from window; return -1 on error */
static int
getextrahints(Window win, Atom prop, unsigned long nmemb, size_t size, void *hints)
{

	unsigned long dl;
	Atom type;
	int di;
	int status, ret;
	unsigned char *p;

	status = XGetWindowProperty(
		dpy, win,
		prop,
		0L, nmemb,
		False,
		prop,
		&type, &di, &dl, &dl,
		&p
	);
	ret = -1;
	if (status == Success && p != NULL) {
		memcpy(hints, p, size);
		ret = 0;
	}
	XFree(p);
	return ret;
}

#define STRCMP(a, b) ((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)

static void
manageunknown(struct Tab *tab, struct Monitor *mon, int desk, Window win,
		Window leader, XRectangle rect, enum State state)
{
	(void)tab;
	(void)mon;
	(void)desk;
	(void)leader;
	(void)rect;
	if (state & BELOW)
		XLowerWindow(dpy, win);
	else if (state & ABOVE)
		XRaiseWindow(dpy, win);
	XMapWindow(dpy, win);
}

struct Class *
getwinclass(Window win, Window *leader, struct Tab **tab, enum State *state, XRectangle *rect, int *desk)
{
	/* rules for identifying windows */
	enum { I_APP, I_CLASS, I_INSTANCE, I_ROLE, I_RESOURCE, I_NULL, I_LAST };
	XrmClass winclass[I_LAST];
	XrmName winname[I_LAST];
	struct MwmHints mwmhints = { 0 };
	struct GNUHints gnuhints = { 0 };
	struct Class *class;
	XClassHint classh = { .res_class = NULL, .res_name = NULL };
	XWMHints *wmhints;
	Atom prop;
	size_t i;
	long n;
	int isdockapp, pos;
	char *role, *value;

	pos = 0;
	*tab = NULL;
	*state = 0;
	class = NULL;
	classh.res_class = NULL;
	classh.res_name = NULL;

	*state = getwinstate(win);

	/* get window type (and other info) from default (hardcoded) rules */
	role = gettextprop(win, atoms[WM_WINDOW_ROLE]);
	XGetClassHint(dpy, win, &classh);
	for (i = 0; config.rules[i].class != NULL || config.rules[i].instance != NULL || config.rules[i].role != NULL; i++) {
		if ((config.rules[i].class == NULL    || STRCMP(config.rules[i].class, classh.res_class))
		&&  (config.rules[i].instance == NULL || STRCMP(config.rules[i].instance, classh.res_name))
		&&  (config.rules[i].role == NULL     || STRCMP(config.rules[i].role, role))) {
			if (config.rules[i].type == TYPE_DOCKAPP) {
				class = dockapp_class;
			}
			if (config.rules[i].state >= 0) {
				*state = config.rules[i].state;
			}
			if (config.rules[i].desktop > 0 && config.rules[i].desktop <= config.ndesktops) {
				*desk = config.rules[i].desktop - 1;
			}
		}
	}

	/* convert strings to quarks for xrm */
	winclass[I_NULL] = winname[I_NULL] = NULLQUARK;
	winclass[I_APP] = wm.application.class;
	winname[I_APP] = wm.application.name;
	if (classh.res_class != NULL)
		winclass[I_CLASS] = winname[I_CLASS] = XrmStringToQuark(classh.res_class);
	else
		winclass[I_CLASS] = winname[I_CLASS] = wm.anyresource;
	if (classh.res_name != NULL)
		winclass[I_INSTANCE] = winname[I_INSTANCE] = XrmStringToQuark(classh.res_name);
	else
		winclass[I_INSTANCE] = winname[I_INSTANCE] = wm.anyresource;
	if (role != NULL)
		winclass[I_ROLE] = winname[I_ROLE] = XrmStringToQuark(role);
	else
		winclass[I_ROLE] = winname[I_ROLE] = wm.anyresource;
	free(role);
	XFree(classh.res_class);
	XFree(classh.res_name);

	/* get window type from X resources */
	winclass[I_RESOURCE] = wm.resources[RES_TYPE].class;
	winname[I_RESOURCE] = wm.resources[RES_TYPE].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL &&
	    strcasecmp(value, "DESKTOP") == 0) {
		class = dockapp_class;
	}

	/* get window state from X resources */
	winclass[I_RESOURCE] = wm.resources[RES_STATE].class;
	winname[I_RESOURCE] = wm.resources[RES_STATE].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		*state = 0;
		if (strcasestr(value, "above") != NULL) {
			*state |= ABOVE;
		}
		if (strcasestr(value, "below") != NULL) {
			*state |= BELOW;
		}
		if (strcasestr(value, "fullscreen") != NULL) {
			*state |= FULLSCREEN;
		}
		if (strcasestr(value, "maximized") != NULL) {
			*state |= MAXIMIZED;
		}
		if (strcasestr(value, "minimized") != NULL) {
			*state |= MINIMIZED;
		}
		if (strcasestr(value, "shaded") != NULL) {
			*state |= SHADED;
		}
		if (strcasestr(value, "sticky") != NULL) {
			*state |= STICKY;
		}
		if (strcasestr(value, "extend") != NULL) {
			*state |= EXTEND;
		}
		if (strcasestr(value, "shrunk") != NULL) {
			*state |= SHRUNK;
		}
		if (strcasestr(value, "resized") != NULL) {
			*state |= RESIZED;
		}
	}

	/* get dockapp position from X resources */
	winclass[I_RESOURCE] = wm.resources[RES_DOCK_POS].class;
	winname[I_RESOURCE] = wm.resources[RES_DOCK_POS].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		if ((n = strtol(value, NULL, 10)) >= 0 && n < INT_MAX) {
			pos = n;
		}
	}

	/* get desktop id from X resources */
	winclass[I_RESOURCE] = wm.resources[RES_DESKTOP].class;
	winname[I_RESOURCE] = wm.resources[RES_DESKTOP].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		if ((n = strtol(value, NULL, 10)) > 0 && n <= config.ndesktops) {
			*desk = n - 1;
		}
	}

	/* we already got the type of the window, return */
	if (class != NULL)
		goto done;

	/* try to guess window type */
	prop = getatomprop(win, atoms[_NET_WM_WINDOW_TYPE]);
	wmhints = XGetWMHints(dpy, win);
	getextrahints(win, atoms[_MOTIF_WM_HINTS], PROP_MWM_HINTS_ELEMENTS, sizeof(mwmhints), &mwmhints);
	getextrahints(win, atoms[_GNUSTEP_WM_ATTR], PROP_GNU_HINTS_ELEMENTS, sizeof(gnuhints), &gnuhints);
	isdockapp = (
		wmhints &&
		FLAG(wmhints->flags, IconWindowHint | StateHint) &&
		wmhints->initial_state == WithdrawnState
	);
	*leader = getwinprop(win, atoms[WM_CLIENT_LEADER]);
	if (*leader == None)
		*leader = (wmhints != NULL && (wmhints->flags & WindowGroupHint)) ? wmhints->window_group : None;
	*tab = getdialogfor(win);
	XFree(wmhints);
	if (!(gnuhints.flags & GNU_FLAG_WINDOWSTYLE))
		gnuhints.window_style = 0;
	if (!(gnuhints.flags & GNU_FLAG_WINDOWLEVEL))
		gnuhints.window_level = 0;
	if (isdockapp ||
	    gnuhints.window_style == GNU_STYLE_ICON ||
	    gnuhints.window_style == GNU_STYLE_MINIWINDOW) {
		class = dockapp_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DESKTOP]) {
		class = unknown_class;
		*state = BELOW;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
		class = notif_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_PROMPT]) {
		class = prompt_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_SPLASH]) {
		class = splash_class;
	} else if (gnuhints.window_level == GNU_LEVEL_POPUP) {
		class = unknown_class;
		*state = ABOVE;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_MENU] ||
	           prop == atoms[_NET_WM_WINDOW_TYPE_UTILITY] ||
	           prop == atoms[_NET_WM_WINDOW_TYPE_TOOLBAR] ||
	           gnuhints.window_level == GNU_LEVEL_PANEL ||
	           gnuhints.window_level == GNU_LEVEL_SUBMENU ||
                   gnuhints.window_level == GNU_LEVEL_MAINMENU ||
                   ((mwmhints.flags & MWM_HINTS_STATUS) &&
                    (mwmhints.status & MWM_TEAROFF_WINDOW))) {
		if (*tab != NULL)
			*leader = (*tab)->obj.win;
		class = menu_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DOCK]) {
		class = bar_class;
	} else if (*tab != NULL) {
		*leader = (*tab)->obj.win;
		class = config.floatdialog ? menu_class : dialog_class;
	} else {
		*tab = getleaderof(*leader);
		if (*tab == NULL)
			*tab = gettabfrompid(getcardprop(win, atoms[_NET_WM_PID]));
		class = tab_class;
	}

done:
	if (class == dockapp_class)
		rect->x = rect->y = pos;
	return class;
}

/* select window input events, grab mouse button presses, and clear its border */
static void
preparewin(Window win)
{
	XSelectInput(dpy, win, StructureNotifyMask|PropertyChangeMask|FocusChangeMask);
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

/* (un)show desktop */
static void
deskshow(int show)
{
	struct Object *obj;
	struct Container *c;

	TAILQ_FOREACH(c, &wm.focusq, entry)
		if (!(c->state & MINIMIZED))
			containerhide(c, show);
	TAILQ_FOREACH(obj, &wm.splashq, entry)
		splashhide((struct Splash *)obj, show);
	wm.showingdesk = show;
	ewmhsetshowingdesktop(show);
	menuupdate();
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
			if (!(c->state & MINIMIZED) && c->desk == desk) {
				containerhide(c, REMOVE);
			} else if (!(c->state & STICKY) && c->desk == mon->seldesk) {
				containerhide(c, ADD);
			}
		}
		TAILQ_FOREACH(obj, &wm.splashq, entry) {
			splash = (struct Splash *)obj;
			if (splash->mon != mon)
				continue;
			if (splash->desk == desk) {
				splashhide(splash, REMOVE);
			} else if (splash->desk == mon->seldesk) {
				splashhide(splash, ADD);
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
manage(Window win, XRectangle rect)
{
	struct Tab *tab;
	struct Class *class;
	Window leader;
	enum State state;
	int desk = wm.selmon->seldesk;

	if (getmanaged(win) != NULL)
		return;
	preparewin(win);
	class = getwinclass(win, &leader, &tab, &state, &rect, &desk);
	(*class->manage)(tab, wm.selmon, desk, win, leader, rect, state);
}

/* perform container switching (aka alt-tab) */
static void
alttab(int shift)
{
	struct Container *c, *prevfocused;
	XEvent ev;

	prevfocused = wm.focused;
	if ((c = TAILQ_FIRST(&wm.focusq)) == NULL)
		return;
	if (XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		goto done;
	if (XGrabPointer(dpy, root, False, 0, GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess)
		goto done;
	containerbacktoplace(c, 0);
	c = containerraisetemp(c, shift);
	while (!XMaskEvent(dpy, KeyPressMask|KeyReleaseMask, &ev)) {
		switch (ev.type) {
		case KeyPress:
			if (ev.xkey.keycode == tabkey && isvalidstate(ev.xkey.state)) {
				containerbacktoplace(c, 1);
				c = containerraisetemp(c, (ev.xkey.state & ShiftMask));
			}
			break;
		case KeyRelease:
			if (ev.xkey.keycode == altkey)
				goto done;
			break;
		}
	}
done:
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	if (c == NULL)
		return;
	containerbacktoplace(c, 1);
	wm.focused = prevfocused;
	tabfocus(c->selcol->selrow->seltab, 0);
	containerraise(c, c->state);
}

/* convert array of state atoms into bitmask of atoms */
static enum State
atoms2state(Atom arr[], size_t siz)
{
	enum State state;
	size_t i;

	state = 0;
	for (i = 0; i < siz; i++) {
		/*
		 * EWMH allows to set two states for a client in a
		 * single client message.  This is a hack to allow
		 * window maximization.
		 *
		 * EWMH does not have any state to represent fully
		 * maximized windows.  Instead, it has two partial
		 * (horizontal and vertical) maximized states.
		 *
		 * A fully maximized window is then represented as
		 * having both states set.
		 *
		 * Since shod does not do partial maximization, we
		 * normalize either as a internal MAXIMIZED state.
		 */
		if (arr[i] == atoms[_NET_WM_STATE_MAXIMIZED_HORZ])
			state |= MAXIMIZED;
		else if (arr[i] == atoms[_NET_WM_STATE_MAXIMIZED_VERT])
			state |= MAXIMIZED;
		else if (arr[i] == atoms[_NET_WM_STATE_ABOVE])
			state |= ABOVE;
		else if (arr[i] == atoms[_NET_WM_STATE_BELOW])
			state |= BELOW;
		else if (arr[i] == atoms[_NET_WM_STATE_FULLSCREEN])
			state |= FULLSCREEN;
		else if (arr[i] == atoms[_NET_WM_STATE_HIDDEN])
			state |= MINIMIZED;
		else if (arr[i] == atoms[_NET_WM_STATE_SHADED])
			state |= SHADED;
		else if (arr[i] == atoms[_NET_WM_STATE_STICKY])
			state |= STICKY;
		else if (arr[i] == atoms[_NET_WM_STATE_DEMANDS_ATTENTION])
			state |= ATTENTION;
		else if (arr[i] == atoms[_SHOD_WM_STATE_STRETCHED])
			state |= STRETCHED;
	}
	return state;
}

/* handle mouse operation, focusing and raising */
static void
xeventbuttonpress(XEvent *e)
{
#define DOUBLE_CLICK_TIME       250
	static Window last_click_window = None;
	static Time last_click_time = 0;
	static unsigned int last_click_button = 0;
	static unsigned int last_click_serial = 1;
	XButtonPressedEvent *event = &e->xbutton;
	struct Object *obj = getmanaged(event->window);
	struct Monitor *mon = getmon(event->x_root, event->y_root);

	if (event->time - last_click_time < DOUBLE_CLICK_TIME &&
	    event->button == last_click_button &&
	    event->window == last_click_window) {
		last_click_serial++;
	} else {
		last_click_serial = 1;
		last_click_button = event->button;
		last_click_window = event->window;
	}
	last_click_time = event->time;

	/* XButtonEvent(3).serial is reporpused as N-uple click index */
	event->serial = last_click_serial;

	if (obj != NULL) {
		btnpress(obj, event);
	} else if (event->window == root && mon != NULL) {
		deskfocus(mon, mon->seldesk);
	}

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
	int perform;
	char buf[16];
	enum { PRESS_CLOSE, PRESS_STACK } action;
	extern char **environ;

	ev = &e->xbutton;
	wm.presswin = None;
	if (ev->button != Button1)
		return;
	if ((obj = getmanaged(ev->window)) == NULL)
		return;
	switch (obj->class->type) {
	case TYPE_NORMAL:
		row = ((struct Tab *)obj)->row;
		if (ev->window == row->br) {
			action = PRESS_CLOSE;
			button = row->br;
		} else if (ev->window == row->bl) {
			action = PRESS_STACK;
			button = row->bl;
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
		drawcommit(button, wm.decorations[FOCUSED].btn_right);
		break;
	case PRESS_STACK:
		(void)snprintf(
			buf,
			sizeof(buf),
			"%lu",
			(unsigned long)row->seltab->obj.win
		);
		(void)setenv("WINDOWID", buf, 1);
		(void)snprintf(
			buf,
			sizeof(buf),
			"%+d%+d",
			row->col->c->x + row->col->x,
			row->col->c->y + row->y + config.titlewidth
		);
		(void)setenv("WINDOWPOS", buf, 1);
		(void)posix_spawnp(
			NULL,
			config.menucmd,
			NULL,
			NULL,
			(char *[]){ config.menucmd, NULL },
			environ
		);
		drawcommit(button, wm.decorations[FOCUSED].btn_left);
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
	//int floattype;
	//void *p;

	ev = &e->xclient;
	if ((obj = getmanaged(ev->window)) != NULL) {
		switch (obj->class->type) {
		case TYPE_NORMAL:
			tab = (struct Tab *)obj;
			c = tab->row->col->c;
			break;
		case TYPE_DIALOG:
			tab = ((struct Dialog *)obj)->tab;
			c = tab->row->col->c;
			break;
		default:
			break;
		}
	}
	if (ev->message_type == atoms[_NET_CURRENT_DESKTOP]) {
		deskfocus(wm.selmon, ev->data.l[0]);
	} else if (ev->message_type == atoms[_SHOD_CYCLE]) {
		alttab(ev->data.l[0]);
	} else if (ev->message_type == atoms[_NET_SHOWING_DESKTOP]) {
		if (ev->data.l[0]) {
			deskshow(1);
		} else {
			deskfocus(wm.selmon, wm.selmon->seldesk);
		}
	} else if (ev->message_type == atoms[_NET_WM_STATE]) {
		if (obj == NULL)
			return;
		if (obj->class->setstate == NULL)
			return;
		(*obj->class->setstate)(
			obj,
			atoms2state((Atom *)&ev->data.l[1], 2),
			ev->data.l[0]
		);
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
			ACTIVATECOL((struct Column *)TAILQ_PREV(&tab->row->col->obj, Queue, entry))
			break;
		case _SHOD_FOCUS_RIGHT_WINDOW:
			ACTIVATECOL((struct Column *)TAILQ_NEXT(&tab->row->col->obj, entry))
			break;
		case _SHOD_FOCUS_TOP_WINDOW:
			ACTIVATEROW((struct Row *)TAILQ_PREV(&tab->row->obj, Queue, entry))
			break;
		case _SHOD_FOCUS_BOTTOM_WINDOW:
			ACTIVATEROW((struct Row *)TAILQ_NEXT(&tab->row->obj, entry))
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
			containerraise(c, c->state);
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
		if (obj->class->type == TYPE_DIALOG) {
			dialogconfigure((struct Dialog *)obj, value_mask, &wc);
		} else if (obj->class->type == TYPE_NORMAL) {
			containerconfigure(c, value_mask, &wc);
		}
	} else if (ev->message_type == atoms[_NET_WM_DESKTOP]) {
		if (obj == NULL || obj->class->type != TYPE_NORMAL)
			return;
		containersendtodeskandfocus(c, c->mon, ev->data.l[0]);
	} else if (ev->message_type == atoms[_NET_REQUEST_FRAME_EXTENTS]) {
		ewmhsetframeextents(ev->window, config.borderwidth, config.titlewidth);
	} else if (ev->message_type == atoms[_NET_WM_MOVERESIZE]) {
	//	/*
	//	 * Client-side decorated Gtk3 windows emit this signal when being
	//	 * dragged by their GtkHeaderBar
	//	 */
	//	if (obj == NULL)
	//		return;
	//	if (obj->class->type == TYPE_MENU) {
	//		p = obj;
	//		floattype = FLOAT_MENU;
	//	} else if (obj->class->type == TYPE_NORMAL) {
	//		p = c;
	//		floattype = FLOAT_CONTAINER;
	//	} else {
	//		return;
	//	}
	//	switch (ev->data.l[2]) {
	//	case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], NW);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_TOP:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], N);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], NE);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_RIGHT:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], E);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], SE);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], S);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], SW);
	//		break;
	//	case _NET_WM_MOVERESIZE_SIZE_LEFT:
	//		mouseresize(floattype, p, ev->data.l[0], ev->data.l[1], W);
	//		break;
	//	case _NET_WM_MOVERESIZE_MOVE:
	//		mousemove(None, floattype, p, ev->data.l[0], ev->data.l[1]);
	//		break;
	//	default:
	//		XUngrabPointer(dpy, CurrentTime);
	//		break;
	//	}
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
		return;
	}
	if (!config.honorconfig)
		return;
	switch (obj->class->type) {
	case TYPE_DIALOG:
		dialogconfigure((struct Dialog *)obj, ev->value_mask, &wc);
		break;
	case TYPE_MENU:
		menuconfigure((struct Menu *)obj, ev->value_mask, &wc);
		break;
	case TYPE_DOCKAPP:
		dockappconfigure((struct Dockapp *)obj, ev->value_mask, &wc);
		break;
	case TYPE_NORMAL:
		containerconfigure(((struct Tab *)obj)->row->col->c, ev->value_mask, &wc);
		break;
	default:
		return;
	}
}

/* forget about client */
static void
xeventdestroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev;
	struct Object *obj;

	ev = &e->xdestroywindow;
	if (ev->window == wm.checkwin) {
		/*
		 * checkwin (the dummy unmapped window used to let other
		 * clients know that Shod is running) has been destroyed
		 * (probably by a call to `shodc exit`).
		 *
		 *
		 * We must exit.
		 */
		wm.running = False;
		return;
	}
	obj = getmanaged(ev->window);
	if (obj == NULL) {
		/*
		 * Destroyed window is not an object we handle.
		 */
		return;
	}
	if (obj->win != ev->window) {
		/*
		 * This SHOULD NOT HAPPEN!
		 *
		 * Destroyed window is a titlebar, button, border or another
		 * part of the frame (aka "non-client area") around client's
		 * window.
		 *
		 * Only shod should create or destroy frame windows.   If we
		 * got here something has gone wrong and an unknown external
		 * client destroyed our resources on our behalf.   Shod will
		 * eventually terminate on a BadWindow or dangling reference
		 * error.
		 */
		return;
	}
	unmanage(obj);
}

/* focus window when cursor enter it (only if there is no focus button) */
static void
xevententernotify(XEvent *e)
{
	struct Tab *tab;
	struct Object *obj;

	if (!config.sloppyfocus && !config.sloppytiles)
		return;
	while (XCheckTypedEvent(dpy, EnterNotify, e))
		;
	if ((obj = getmanaged(e->xcrossing.window)) == NULL)
		return;
	if (obj->class->type == TYPE_DIALOG)
		tab = ((struct Dialog *)obj)->tab;
	else if (obj->class->type == TYPE_NORMAL)
		tab = (struct Tab *)obj;
	else
		return;
	if (!config.sloppytiles && tab->row->col->c == wm.focused)
		return;
	if (!config.sloppyfocus && tab->row->col->c != wm.focused)
		return;
	if (config.sloppyfocus && tab->row->col->c != wm.focused)
		tab = tab->row->col->c->selcol->selrow->seltab;
	tabfocus(tab, 1);
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
	switch (obj->class->type) {
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
	default:
		return;
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
	if (!config.disablealttab && ev->keycode == tabkey && isvalidstate(ev->state)) {
		alttab(ev->state & ShiftMask);
	}
	if (ev->window == wm.checkwin) {
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
		}
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
	struct Container *c;
	struct Object *obj;
	struct Tab *tab;
	struct Menu *menu;
	char *str;

	ev = &e->xproperty;
	if (ev->state != PropertyNewValue)
		return;
	if (ev->window == root && ev->atom == XA_RESOURCE_MANAGER) {
		if ((str = gettextprop(root, XA_RESOURCE_MANAGER)) == NULL)
			return;
		XrmDestroyDatabase(xdb);
		setresources(str);
		free(str);
		TAILQ_FOREACH(c, &wm.focusq, entry)
			containerdecorate(c);
		TAILQ_FOREACH(obj, &wm.menuq, entry)
			menudecorate((struct Menu *)obj);
		TAILQ_FOREACH(obj, &wm.notifq, entry)
			notifdecorate((struct Notification *)obj);
		dockreset();
		return;
	}
	obj = getmanaged(ev->window);
	if (obj == NULL)
		return;
	if (obj->class->type == TYPE_NORMAL && ev->window == obj->win) {
		tab = (struct Tab *)obj;
		if (ev->atom == XA_WM_NAME || ev->atom == atoms[_NET_WM_NAME]) {
			winupdatetitle(tab->obj.win, &tab->name);
			tabdecorate(tab, 0);
		} else if (ev->atom == XA_WM_HINTS) {
			tabupdateurgency(tab, getwinurgency(tab->obj.win));
		}
	} else if (obj->class->type == TYPE_BAR && (ev->atom == _NET_WM_STRUT_PARTIAL || ev->atom == _NET_WM_STRUT)) {
		barstrut((struct Bar *)obj);
		monupdatearea();
	} else if (obj->class->type == TYPE_MENU && ev->window == obj->win) {
		menu = (struct Menu *)obj;
		if (ev->atom == XA_WM_NAME || ev->atom == atoms[_NET_WM_NAME]) {
			winupdatetitle(menu->obj.win, &menu->name);
			menudecorate(menu);
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
	obj = getmanaged(ev->window);
	if (obj == NULL || obj->win != ev->window) {
		/*
		 * Unmapped window is not the client window of an object
		 * we handle.
		 */
		return;
	}
	if (ev->event == root) {
		/*
		 * Ignore unmap notifications reported relative the root
		 * window (if we have selected SubstructureNotifyMask on
		 * the root window at shod.c).
		 *
		 * Since we select StructureNotifyMask on client windows,
		 * unmap notifications are reported to us relative to the
		 * client window itself.
		 *
		 * If we get a unmap notification relative to root window
		 * (supposed we had selected SubstructureNotifyMask on it),
		 * we have most likely unselected StructureNotifyMask on
		 * the client window temporarily.
		 *
		 * [SubstructureNotifyMask is not selected on the root
		 *  window anymore for it seemed redundant; so we will
		 *  probably never reach this point of the function.
		 *  Check shod.c:/checkotherwm/ for more information.]
		 */
		return;
	}
	unmanage(obj);
}

/* scan for already existing windows and adopt them */
void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, transwin, *wins = NULL;
	XWindowAttributes wa;

	/*
	 * No event can be processed while winodws are managed.
	 */
	XGrabServer(dpy);
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
					}
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
					}
				);
			}
		}
		if (wins != NULL) {
			XFree(wins);
		}
	}
	XSync(dpy, True);
	XUngrabServer(dpy);
}

/* set modifier and Alt key code from given key sym */
void
setmod(void)
{
	size_t i;
	static unsigned int lock_modifiers[] = {
		/* CapsLk | NumLk     | ScrLk  */
		0         | 0         | 0       ,
		0         | 0         | Mod3Mask,
		0         | Mod2Mask  | 0       ,
		0         | Mod2Mask  | Mod3Mask,
		LockMask  | 0         | 0       ,
		LockMask  | 0         | Mod3Mask,
		LockMask  | Mod2Mask  | 0       ,
		LockMask  | Mod2Mask  | Mod3Mask,
	};

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	if ((altkey = XKeysymToKeycode(dpy, config.altkeysym)) == 0) {
		warnx("could not get keycode from keysym");
		return;
	}
	if ((tabkey = XKeysymToKeycode(dpy, config.tabkeysym)) == 0) {
		warnx("could not get keycode from keysym");
		return;
	}
	if (config.disablealttab)
		return;
	for (i = 0; i < LEN(lock_modifiers); i++) {
		/* alt+tab */
		XGrabKey(
			dpy,
			tabkey,
			config.modifier | lock_modifiers[i],
			root,
			False,
			GrabModeAsync,
			GrabModeAsync
		);
		/* alt+shift+tab */
		XGrabKey(
			dpy,
			tabkey,
			config.modifier | ShiftMask | lock_modifiers[i],
			root,
			False,
			GrabModeAsync,
			GrabModeAsync
		);
	}
}

void (*xevents[LASTEvent])(XEvent *) = {
	[ButtonPress]      = xeventbuttonpress,
	[ButtonRelease]    = xeventbuttonrelease,
	[ClientMessage]    = xeventclientmessage,
	[ConfigureRequest] = xeventconfigurerequest,
	[DestroyNotify]    = xeventdestroynotify,
	[EnterNotify]      = xevententernotify,
	[FocusIn]          = xeventfocusin,
	[KeyPress]         = xeventkeypress,
	[MapRequest]       = xeventmaprequest,
	[MappingNotify]    = xeventmappingnotify,
	[PropertyNotify]   = xeventpropertynotify,
	[UnmapNotify]      = xeventunmapnotify,
};
