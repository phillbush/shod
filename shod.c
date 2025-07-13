#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/XKBlib.h>
#include <X11/extensions/Xrandr.h>

#include "shod.h"

/* call instance method, if it exists */
#define ARG1(arg, ...) (arg)
#define CALL_METHOD(method, ...) \
	if (ARG1(__VA_ARGS__, 0)->class->method != NULL) \
		ARG1(__VA_ARGS__, 0)->class->method(__VA_ARGS__)

/* for each class, call class method, if it exists */
#define FOREACH_CLASS(method, ...) \
	for (size_t i = 0; i < LEN(classes); i++) \
		if (classes[i]->method != NULL) \
			classes[i]->method(__VA_ARGS__)

#define RESOURCES                                                                        \
	/*                      CLASS                        NAME                      */\
	X(RES_TYPE,            "Type",                      "type"                      )\
	X(RES_STATE,           "State",                     "state"                     )\
	X(RES_DESKTOP,         "Desktop",                   "desktop"                   )\
	X(RES_DOCK_POS,        "Dockpos",                   "dockpos"                   )\
	X(RES_FACE_NAME,       "FaceName",                  "faceName"                  )\
	X(RES_FOREGROUND,      "Foreground",                "foreground"                )\
	X(RES_DOCK_BACKGROUND, "DockBackground",            "dockBackground"            )\
	X(RES_DOCK_BORDER,     "DockBorder",                "dockBorder"                )\
	X(RES_ACTIVE_BG,       "ActiveBackground",          "activeBackground"          )\
	X(RES_ACTIVE_TOP,      "ActiveTopShadowColor",      "activeTopShadowColor"      )\
	X(RES_ACTIVE_BOT,      "ActiveBottomShadowColor",   "activeBottomShadowColor"   )\
	X(RES_INACTIVE_BG,     "InactiveBackground",        "inactiveBackground"        )\
	X(RES_INACTIVE_TOP,    "InactiveTopShadowColor",    "inactiveTopShadowColor"    )\
	X(RES_INACTIVE_BOT,    "InactiveBottomShadowColor", "inactiveBottomShadowColor" )\
	X(RES_URGENT_BG,       "UrgentBackground",          "urgentBackground"          )\
	X(RES_URGENT_TOP,      "UrgentTopShadowColor",      "urgentTopShadowColor"      )\
	X(RES_URGENT_BOT,      "UrgentBottomShadowColor",   "urgentBottomShadowColor"   )\
	X(RES_BORDER_WIDTH,    "BorderWidth",               "borderWidth"               )\
	X(RES_SHADOW_WIDTH,    "ShadowThickness",           "shadowThickness"           )\
	X(RES_TITLE_WIDTH,     "TitleWidth",                "titleWidth"                )\
	X(RES_DOCK_WIDTH,      "DockWidth",                 "dockWidth"                 )\
	X(RES_DOCK_SPACE,      "DockSpace",                 "dockSpace"                 )\
	X(RES_DOCK_GRAVITY,    "DockGravity",               "dockGravity"               )\
	X(RES_NOTIFY_GAP,      "NotifGap",                  "notifGap"                  )\
	X(RES_NOTIFY_GRAVITY,  "NotifGravity",              "notifGravity"              )\
	X(RES_NDESKTOPS,       "NumOfDesktops",             "numOfDesktops"             )\
	X(RES_SNAP_PROXIMITY,  "SnapProximity",             "snapProximity"             )\
	X(RES_MOVE_TIME,       "MoveTime",                  "moveTime"                  )\
	X(RES_RESIZE_TIME,     "ResizeTime",                "resizeTime"                )

enum Resource {
#define X(res, class, name) res,
	RESOURCES
	NRESOURCES
#undef  X
};

static void manageunknown(struct Tab *, struct Monitor *, int, Window,
		Window, XRectangle, enum State);

static struct Class *classes[] = {
	&bar_class,
	&dialog_class,
	&dockapp_class,
	&menu_class,
	&notif_class,
	&prompt_class,
	&splash_class,
	&tab_class,
	&container_class,
};

static KeyCode altkey;
static KeyCode tabkey;
static XContext context;
static Bool running = True;
static struct {
	XrmClass class;
	XrmName name;
} application, resources[NRESOURCES];
static XrmQuark anyresource;

Display *dpy;
Window root;
Atom atoms[NATOMS];
int screen;
Visual *visual;
Colormap colormap;
unsigned int depth;
XrmDatabase xdb = NULL;
GC gc;
struct Theme theme;

struct WM wm = { 0 };

static void
usage(void)
{
	(void)fprintf(stderr, "usage: shod [-AcdhSstW] [file]\n");
	exit(1);
}

static Bool
is_userplaced(Window win)
{
	XSizeHints hints;

	if (XGetWMNormalHints(dpy, win, &hints, &(long){0}))
		return hints.flags&USPosition;
	return False;
}

/* check if desktop is visible */
static int
deskisvisible(struct Monitor *mon, int desk)
{
	return mon->seldesk == desk;
}

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
		obj = context_get(tmpwin);
		if (obj == NULL)
			return NULL;
		if (obj->class != &tab_class)
			return NULL;
		return obj->self;
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
	if (is_userplaced(win))
		state |= USERPLACED;
	XFree(list);
	return state;
}

/* get window's WM_STATE property */
static long
getstate(Window w)
{
	long state = -1;
	void *p = NULL;

	if (getprop(dpy, w, atoms[WM_STATE], atoms[WM_STATE], 32, 1, &p) == 1)
		state = *(long *)p;
	XFree(p);
	return state;
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

static char *
getresource(XrmDatabase xdb, XrmClass *class, XrmName *name)
{
	XrmRepresentation tmp;
	XrmValue xval;

	if (xdb == NULL)
		return NULL;
	if (XrmQGetResource(xdb, name, class, &tmp, &xval))
		return xval.addr;
	return NULL;
}

static struct Class *
getwinclass(Window win, Window *leader, struct Tab **tab, enum State *state, XRectangle *rect, int *desk)
{
	static struct Class unknown_class = {
		.setstate       = NULL,
		.manage         = &manageunknown,
		.unmanage       = NULL,
	};

	enum MotifWM_constants {
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

	enum GNUstep_constants {
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

	struct MwmHints {
		unsigned long flags;
		unsigned long functions;
		unsigned long decorations;
		long          inputMode;
		unsigned long status;
	};

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
			if (config.rules[i].type != NULL) {
				class = config.rules[i].type;
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
	winclass[I_APP] = application.class;
	winname[I_APP] = application.name;
	if (classh.res_class != NULL)
		winclass[I_CLASS] = winname[I_CLASS] = XrmStringToQuark(classh.res_class);
	else
		winclass[I_CLASS] = winname[I_CLASS] = anyresource;
	if (classh.res_name != NULL)
		winclass[I_INSTANCE] = winname[I_INSTANCE] = XrmStringToQuark(classh.res_name);
	else
		winclass[I_INSTANCE] = winname[I_INSTANCE] = anyresource;
	if (role != NULL)
		winclass[I_ROLE] = winname[I_ROLE] = XrmStringToQuark(role);
	else
		winclass[I_ROLE] = winname[I_ROLE] = anyresource;
	free(role);
	XFree(classh.res_class);
	XFree(classh.res_name);

	/* get window type from X resources */
	winclass[I_RESOURCE] = resources[RES_TYPE].class;
	winname[I_RESOURCE] = resources[RES_TYPE].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL &&
	    strcasecmp(value, "DESKTOP") == 0) {
		class = &dockapp_class;
	}

	/* get window state from X resources */
	winclass[I_RESOURCE] = resources[RES_STATE].class;
	winname[I_RESOURCE] = resources[RES_STATE].name;
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
	winclass[I_RESOURCE] = resources[RES_DOCK_POS].class;
	winname[I_RESOURCE] = resources[RES_DOCK_POS].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		if ((n = strtol(value, NULL, 10)) >= 0 && n < INT_MAX) {
			pos = n;
		}
	}

	/* get desktop id from X resources */
	winclass[I_RESOURCE] = resources[RES_DESKTOP].class;
	winname[I_RESOURCE] = resources[RES_DESKTOP].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		if ((n = strtol(value, NULL, 10)) > 0 && n <= config.ndesktops) {
			*desk = n - 1;
		}
	}

	/* we already got the type of the window, return */
	if (class != NULL)
		goto done;

	/* try to guess window type */
	prop = getatomprop(dpy, win, atoms[_NET_WM_WINDOW_TYPE]);
	wmhints = XGetWMHints(dpy, win);
	getextrahints(win, atoms[_MOTIF_WM_HINTS], PROP_MWM_HINTS_ELEMENTS, sizeof(mwmhints), &mwmhints);
	getextrahints(win, atoms[_GNUSTEP_WM_ATTR], PROP_GNU_HINTS_ELEMENTS, sizeof(gnuhints), &gnuhints);
	isdockapp = (
		wmhints &&
		FLAG(wmhints->flags, IconWindowHint | StateHint) &&
		wmhints->initial_state == WithdrawnState
	);
	*leader = getwinprop(dpy, win, atoms[WM_CLIENT_LEADER]);
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
		class = &dockapp_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DESKTOP]) {
		class = &unknown_class;
		*state = BELOW;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
		class = &notif_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_PROMPT]) {
		class = &prompt_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_SPLASH]) {
		class = &splash_class;
	} else if (gnuhints.window_level == GNU_LEVEL_POPUP) {
		class = &unknown_class;
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
		class = &menu_class;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DOCK]) {
		class = &bar_class;
	} else if (*tab != NULL) {
		*leader = (*tab)->obj.win;
		class = config.floatdialog ? &menu_class : &dialog_class;
	} else {
		*tab = getleaderof(*leader);
		if (*tab == NULL)
			*tab = gettabfrompid(getcardprop(dpy, win, atoms[_NET_WM_PID]));
		class = &tab_class;
	}

done:
	if (class == &dockapp_class)
		rect->x = rect->y = pos;
	return class;
}

static void
deskshow(long show)
{
	if (show) {
		FOREACH_CLASS(show_desktop);
	} else {
		FOREACH_CLASS(hide_desktop);
	}
	wm.showingdesk = show;
	XChangeProperty(
		dpy, root, atoms[_NET_SHOWING_DESKTOP],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)&show, 1
	);
}

static void
deskfocus(struct Monitor *mon, int desk)
{
	if (desk < 0 || desk >= config.ndesktops || (mon == wm.selmon && desk == wm.selmon->seldesk))
		return;
	deskupdate(mon, desk);
	focusnext(mon, desk);
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

	if (context_get(win) != NULL)
		return;
	XSelectInput(dpy, win, StructureNotifyMask|PropertyChangeMask|FocusChangeMask);
	XSetWindowBorderWidth(dpy, win, 0);
	class = getwinclass(win, &leader, &tab, &state, &rect, &desk);
	(*class->manage)(tab, wm.selmon, desk, win, leader, rect, state);
}

/* set modifier and Alt key code from given key sym */
static void
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

static char *
queryrdb(int res)
{
	XrmClass class[] = { application.class, resources[res].class, NULLQUARK };
	XrmName name[] = { application.name, resources[res].name, NULLQUARK };

	return getresource(xdb, class, name);
}

static int
alloccolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color)) {
		warnx("could not allocate color: %s", s);
		return 0;
	}
	return 1;
}

static void
setcolor(char *value, int style, int ncolor)
{
	XftColor color;

	if (!alloccolor(value, &color))
		return;
	XftColorFree(dpy, visual, colormap, &theme.colors[style][ncolor]);
	theme.colors[style][ncolor] = color;
}

static XftFont *
openfont(const char *s)
{
	XftFont *font = NULL;

	if ((font = XftFontOpenXlfd(dpy, screen, s)) == NULL)
		if ((font = XftFontOpenName(dpy, screen, s)) == NULL)
			warnx("could not open font: %s", s);
	return font;
}

static void
reloadtheme(void)
{
	Pixmap pix;

	config.corner = config.borderwidth + config.titlewidth;
	config.divwidth = config.borderwidth;
	wm.minsize = config.corner * 2 + 10;
	if (config.borderwidth > 5)
		config.shadowthickness = 2;     /* thick shadow */
	else
		config.shadowthickness = 1;     /* slim shadow */

	FOREACH_CLASS(reload_theme);

	pix = XCreatePixmap(
		dpy,
		wm.dragwin,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		depth
	);
	drawbackground(
		pix,
		0, 0,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		FOCUSED
	);
	drawborders(
		pix,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		FOCUSED
	);
	drawshadow(
		pix,
		config.borderwidth,
		config.borderwidth,
		config.titlewidth,
		config.titlewidth,
		FOCUSED, 0, config.shadowthickness
	);
	XMoveResizeWindow(
		dpy,
		wm.dragwin,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth),
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth
	);
	XSetWindowBackgroundPixmap(dpy, wm.dragwin, pix);
	XClearWindow(dpy, wm.dragwin);
	XFreePixmap(dpy, pix);
}

static void
setresources(char *xrm)
{
	enum Resource resource;

	xdb = NULL;
	if (xrm == NULL || (xdb = XrmGetStringDatabase(xrm)) == NULL)
		return;
	for (resource = 0; resource < NRESOURCES; resource++) {
		char *value = queryrdb(resource);
		XftFont *font;
		long n;

		if (value == NULL)
			continue;
		switch (resource) {
		case RES_FACE_NAME:
			if ((font = openfont(value)) != NULL) {
				XftFontClose(dpy, theme.font);
				theme.font = font;
			}
			break;
		case RES_FOREGROUND:
			setcolor(value, STYLE_OTHER, COLOR_FG);
			break;
		case RES_DOCK_BACKGROUND:
			setcolor(value, STYLE_OTHER, COLOR_BG);
			break;
		case RES_DOCK_BORDER:
			setcolor(value, STYLE_OTHER, COLOR_BORD);
			break;
		case RES_ACTIVE_BG:
			setcolor(value, FOCUSED, COLOR_MID);
			break;
		case RES_ACTIVE_TOP:
			setcolor(value, FOCUSED, COLOR_LIGHT);
			break;
		case RES_ACTIVE_BOT:
			setcolor(value, FOCUSED, COLOR_DARK);
			break;
		case RES_INACTIVE_BG:
			setcolor(value, UNFOCUSED, COLOR_MID);
			break;
		case RES_INACTIVE_TOP:
			setcolor(value, UNFOCUSED, COLOR_LIGHT);
			break;
		case RES_INACTIVE_BOT:
			setcolor(value, UNFOCUSED, COLOR_DARK);
			break;
		case RES_URGENT_BG:
			setcolor(value, URGENT, COLOR_MID);
			break;
		case RES_URGENT_TOP:
			setcolor(value, URGENT, COLOR_LIGHT);
			break;
		case RES_URGENT_BOT:
			setcolor(value, URGENT, COLOR_DARK);
			break;
		case RES_BORDER_WIDTH:
			if ((n = strtol(value, NULL, 10)) >= 3 && n <= 16)
				config.borderwidth = n;
			break;
		case RES_TITLE_WIDTH:
			if ((n = strtol(value, NULL, 10)) >= 3 && n <= 32)
				config.titlewidth = n;
			break;
		case RES_DOCK_WIDTH:
			if ((n = strtol(value, NULL, 10)) >= 16 && n <= 256)
				config.dockwidth = n;
			break;
		case RES_DOCK_SPACE:
			if ((n = strtol(value, NULL, 10)) >= 16 && n <= 256)
				config.dockspace = n;
			break;
		case RES_DOCK_GRAVITY:
			config.dockgravity = value;
			break;
		case RES_NOTIFY_GAP:
			if ((n = strtol(value, NULL, 10)) >= 0 && n <= 64)
				config.notifgap = n;
			break;
		case RES_NOTIFY_GRAVITY:
			config.notifgravity = value;
			break;
		case RES_SNAP_PROXIMITY:
			if ((n = strtol(value, NULL, 10)) >= 0 && n < 64)
				config.snap = n;
			break;
		case RES_MOVE_TIME:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.movetime = n;
			break;
		case RES_RESIZE_TIME:
			if ((n = strtol(value, NULL, 10)) > 0)
				config.resizetime = n;
			break;
		case RES_NDESKTOPS:
			if ((n = strtol(value, NULL, 10)) > 0 && n < 100)
				config.ndesktops = n;
			break;
		default:
			break;
		}
	}
	reloadtheme();
}

static Bool
set_theme(void)
{
	for (int i = 0; i < STYLE_LAST; i++)
		for (int j = 0; j < COLOR_LAST; j++)
			if (!alloccolor(config.colors[i][j], &theme.colors[i][j]))
				return False;
	if ((theme.font = openfont(config.font)) == NULL)
		return False;
	reloadtheme();
	return True;
}

static void
xeventbuttonpress(XEvent *event)
{
#define DOUBLE_CLICK_TIME       250
	static Window last_click_window = None;
	static Time last_click_time = 0;
	static unsigned int last_click_button = 0;
	static unsigned int last_click_serial = 1;
	XButtonPressedEvent *press = &event->xbutton;
	struct Object *obj = context_get(press->window);
	struct Monitor *mon = getmon(press->x_root, press->y_root);

	if (press->time - last_click_time < DOUBLE_CLICK_TIME &&
	    press->button == last_click_button &&
	    press->window == last_click_window) {
		last_click_serial++;
	} else {
		last_click_serial = 1;
		last_click_button = press->button;
		last_click_window = press->window;
	}
	last_click_time = press->time;

	/* XButtonEvent(3).serial is reporpused as N-uple click index */
	press->serial = last_click_serial;

	if (obj != NULL) {
		CALL_METHOD(btnpress, obj, press);
	} else if (press->window == root && mon != NULL) {
		deskfocus(mon, mon->seldesk);
	}

	XAllowEvents(dpy, ReplayPointer, CurrentTime);
}

static void
xeventclientmessage(XEvent *event)
{
	XClientMessageEvent *message = &event->xclient;
	struct Object *obj;

	if (message->message_type == atoms[_NET_ACTIVE_WINDOW] &&
	    message->window == None) {
		CALL_METHOD(
			handle_message, wm.focused,
			message->message_type, message->data.l
		);
	} else if (message->message_type == atoms[_NET_REQUEST_FRAME_EXTENTS]) {
		XChangeProperty(
			dpy, message->window, atoms[_NET_FRAME_EXTENTS],
			XA_CARDINAL, 32, PropModeReplace,
			(void *)(long[]){
				config.borderwidth,
				config.borderwidth,
				config.borderwidth + config.titlewidth,
				config.borderwidth,
			}, 4
		);
	} else if (message->message_type == atoms[_NET_CURRENT_DESKTOP]) {
		deskfocus(wm.selmon, message->data.l[0]);
	} else if (message->message_type == atoms[_SHOD_CYCLE]) {
		alttab(altkey, tabkey, message->data.l[0]);
	} else if (message->message_type == atoms[_NET_SHOWING_DESKTOP]) {
		deskshow(message->data.l[0]);
	} else if (message->message_type == atoms[_NET_CLOSE_WINDOW]) {
		window_close(dpy, message->window);
	} else if ((obj = context_get(message->window)) != NULL &&
	         obj->win == message->window) {
		CALL_METHOD(
			handle_message, obj,
			message->message_type, message->data.l
		);
	}
}

static void
xeventconfigurerequest(XEvent *event)
{
	struct Object *obj;
	XConfigureRequestEvent *configure = &event->xconfigurerequest;
	XWindowChanges changes = {
		.x	= configure->x,
		.y	= configure->y,
		.width	= configure->width,
		.height	= configure->height,
		.border_width	= configure->border_width,
		.sibling	= configure->above,
		.stack_mode	= configure->detail,
	};

	if ((obj = context_get(configure->window)) == NULL) {
		XConfigureWindow(
			dpy,
			configure->window, configure->value_mask,
			&changes
		);
	} else if (config.honorconfig) {
		CALL_METHOD(handle_configure, obj, configure->value_mask, &changes);
	}
}

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
		running = False;
		return;
	}
	obj = context_get(ev->window);
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
	CALL_METHOD(unmanage, obj);
	XDeleteProperty(dpy, obj->win, atoms[WM_STATE]);
}

static void
xevententernotify(XEvent *e)
{
	struct Object *obj;

	/* focus window when cursor enter it (only if there is no focus button) */
	if (!config.sloppyfocus && !config.sloppytiles)
		return;
	while (XCheckTypedEvent(dpy, EnterNotify, e))
		;
	if ((obj = context_get(e->xcrossing.window)) == NULL)
		return;
	CALL_METHOD(handle_enter, obj);
}

static void
xeventfocusin(XEvent *e)
{
#warning TODO: handle focus stealing
	(void)e;
}

static void
xeventkeypress(XEvent *e)
{
	XKeyPressedEvent *ev;

	ev = &e->xkey;
	if (!config.disablealttab && ev->keycode == tabkey && isvalidstate(ev->state)) {
		alttab(altkey, tabkey, ev->state & ShiftMask);
	}
	if (ev->window == wm.checkwin) {
		e->xkey.window = root;
		XSendEvent(dpy, root, False, KeyPressMask, e);
	}
}

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

static void
xeventmappingnotify(XEvent *e)
{
	(void)e;
	setmod();
}

static void
xeventpropertynotify(XEvent *e)
{
	XPropertyEvent *event = &e->xproperty;
	struct Object *obj;
	char *str;

	if (event->state != PropertyNewValue)
		return;
	if (event->window == root && event->atom == XA_RESOURCE_MANAGER) {
		if ((str = gettextprop(root, XA_RESOURCE_MANAGER)) == NULL)
			return;
		XrmDestroyDatabase(xdb);
		setresources(str);
		free(str);
		FOREACH_CLASS(redecorate_all);
	} else if (
		(obj = context_get(event->window)) != NULL &&
		event->window == obj->win
	) {
		CALL_METHOD(handle_property, obj, event->atom);
	}
}

static void
xeventunmapnotify(XEvent *e)
{
	XUnmapEvent *ev;
	struct Object *obj;

	ev = &e->xunmap;
	obj = context_get(ev->window);
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
	CALL_METHOD(unmanage, obj);
}

/* scan for already existing windows and adopt them */
static void
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

	/*
	 * The focus-holding window must be mapped after all already-mapped
	 * windows get scanned
	 */
	XMapWindow(dpy, wm.focuswin);
	XSetInputFocus(dpy, wm.focuswin, RevertToParent, CurrentTime);
}

/* call fork checking for error; exit on error */
static pid_t
efork(void)
{
	pid_t pid;

	if ((pid = fork()) < 0)
		err(1, "fork");
	return pid;
}

/* call execlp checking for error; exit on error */
static void
execshell(char *filename)
{
	char *argv[3];

	if ((argv[0] = getenv("SHELL")) == NULL)
		argv[0] = "sh";
	if (filename[0] == '-' && filename[1] == '\0')
		argv[1] = NULL;         /* read commands from stdin */
	else
		argv[1] = filename;     /* read commands from file */
	argv[2] = NULL;
	if (execvp(argv[0], argv) == -1) {
		err(1, "%s", argv[0]);
	}
}

/* error handler */
static int
xerror(Display *dpy, XErrorEvent *error)
{
	/*
	 * Request names and error messages copied from the X Error
	 * Database at </usr/share/X11/XErrorDB>.
	 *
	 * Only requests/errors from the core X11 protocol are issued on
	 * error messages.  Those from extensions are shown as "unknown
	 * request/error"
	 *
	 * I could use XGetErrorDatabaseText(3) here to read those
	 * strings from the XErrorDB at runtime, but its buffer-filling
	 * interface is ugly (like most of XLib).
	 *
	 * I could also use the default error string from the default
	 * error handler, but it issues a generic multi-line error
	 * message, which can be easily mixed with the error message of
	 * another X client.
	 */
	static const char *request_names[] = {
		[0]	= "unknown request",
		[1]	= "CreateWindow",
		[2]	= "ChangeWindowAttributes",
		[3]	= "GetWindowAttributes",
		[4]	= "DestroyWindow",
		[5]	= "DestroySubwindows",
		[6]	= "ChangeSaveSet",
		[7]	= "ReparentWindow",
		[8]	= "MapWindow",
		[9]	= "MapSubwindows",
		[10]	= "UnmapWindow",
		[11]	= "UnmapSubwindows",
		[12]	= "ConfigureWindow",
		[13]	= "CirculateWindow",
		[14]	= "GetGeometry",
		[15]	= "QueryTree",
		[16]	= "InternAtom",
		[17]	= "GetAtomName",
		[18]	= "ChangeProperty",
		[19]	= "DeleteProperty",
		[20]	= "GetProperty",
		[21]	= "ListProperties",
		[22]	= "SetSelectionOwner",
		[23]	= "GetSelectionOwner",
		[24]	= "ConvertSelection",
		[25]	= "SendEvent",
		[26]	= "GrabPointer",
		[27]	= "UngrabPointer",
		[28]	= "GrabButton",
		[29]	= "UngrabButton",
		[30]	= "ChangeActivePointerGrab",
		[31]	= "GrabKeyboard",
		[32]	= "UngrabKeyboard",
		[33]	= "GrabKey",
		[34]	= "UngrabKey",
		[35]	= "AllowEvents",
		[36]	= "GrabServer",
		[37]	= "UngrabServer",
		[38]	= "QueryPointer",
		[39]	= "GetMotionEvents",
		[40]	= "TranslateCoords",
		[41]	= "WarpPointer",
		[42]	= "SetInputFocus",
		[43]	= "GetInputFocus",
		[44]	= "QueryKeymap",
		[45]	= "OpenFont",
		[46]	= "CloseFont",
		[47]	= "QueryFont",
		[48]	= "QueryTextExtents",
		[49]	= "ListFonts",
		[50]	= "ListFontsWithInfo",
		[51]	= "SetFontPath",
		[52]	= "GetFontPath",
		[53]	= "CreatePixmap",
		[54]	= "FreePixmap",
		[55]	= "CreateGC",
		[56]	= "ChangeGC",
		[57]	= "CopyGC",
		[58]	= "SetDashes",
		[59]	= "SetClipRectangles",
		[60]	= "FreeGC",
		[61]	= "ClearArea",
		[62]	= "CopyArea",
		[63]	= "CopyPlane",
		[64]	= "PolyPoint",
		[65]	= "PolyLine",
		[66]	= "PolySegment",
		[67]	= "PolyRectangle",
		[68]	= "PolyArc",
		[69]	= "FillPoly",
		[70]	= "PolyFillRectangle",
		[71]	= "PolyFillArc",
		[72]	= "PutImage",
		[73]	= "GetImage",
		[74]	= "PolyText8",
		[75]	= "PolyText16",
		[76]	= "ImageText8",
		[77]	= "ImageText16",
		[78]	= "CreateColormap",
		[79]	= "FreeColormap",
		[80]	= "CopyColormapAndFree",
		[81]	= "InstallColormap",
		[82]	= "UninstallColormap",
		[83]	= "ListInstalledColormaps",
		[84]	= "AllocColor",
		[85]	= "AllocNamedColor",
		[86]	= "AllocColorCells",
		[87]	= "AllocColorPlanes",
		[88]	= "FreeColors",
		[89]	= "StoreColors",
		[90]	= "StoreNamedColor",
		[91]	= "QueryColors",
		[92]	= "LookupColor",
		[93]	= "CreateCursor",
		[94]	= "CreateGlyphCursor",
		[95]	= "FreeCursor",
		[96]	= "RecolorCursor",
		[97]	= "QueryBestSize",
		[98]	= "QueryExtension",
		[99]	= "ListExtensions",
		[100]	= "ChangeKeyboardMapping",
		[101]	= "GetKeyboardMapping",
		[102]	= "ChangeKeyboardControl",
		[103]	= "GetKeyboardControl",
		[104]	= "Bell",
		[105]	= "ChangePointerControl",
		[106]	= "GetPointerControl",
		[107]	= "SetScreenSaver",
		[108]	= "GetScreenSaver",
		[109]	= "ChangeHosts",
		[110]	= "ListHosts",
		[111]	= "SetAccessControl",
		[112]	= "SetCloseDownMode",
		[113]	= "KillClient",
		[114]	= "RotateProperties",
		[115]	= "ForceScreenSaver",
		[116]	= "SetPointerMapping",
		[117]	= "GetPointerMapping",
		[118]	= "SetModifierMapping",
		[119]	= "GetModifierMapping",
	};
	static const char *error_messages[] = {
		[0]	= "unknown error",
		[1]	= "Invalid request code",
		[2]	= "BadValue (integer parameter out of range for operation)",
		[3]	= "BadWindow (invalid Window parameter)",
		[4]	= "BadPixmap (invalid Pixmap parameter)",
		[5]	= "BadAtom (invalid Atom parameter)",
		[6]	= "BadCursor (invalid Cursor parameter)",
		[7]	= "BadFont (invalid Font parameter)",
		[8]	= "BadMatch (invalid parameter attributes)",
		[9]	= "BadDrawable (invalid Pixmap or Window parameter)",
		[10]	= "BadAccess (attempt to access private resource denied)",
		[11]	= "BadAlloc (insufficient resources for operation)",
		[12]	= "BadColor (invalid Colormap parameter)",
		[13]	= "BadGC (invalid GC parameter)",
		[14]	= "BadIDChoice (invalid resource ID chosen for this connection)",
		[15]	= "BadName (named color or font does not exist)",
		[16]	= "BadLength (poly request too large or internal Xlib length error)",
		[17]	= "BadImplementation (server does not implement operation)",
	};

	(void)dpy;
	/* There's no way to check accesses to destroyed windows, thus those
	 * cases are ignored (especially on UnmapNotify's). */
	if (error->error_code == BadWindow ||
	    (error->request_code == X_SetInputFocus && error->error_code == BadMatch) ||
	    (error->request_code == X_PolyText8 && error->error_code == BadDrawable) ||
	    (error->request_code == X_PolyFillRectangle && error->error_code == BadDrawable) ||
	    (error->request_code == X_PolySegment && error->error_code == BadDrawable) ||
	    (error->request_code == X_ConfigureWindow && error->error_code == BadMatch) ||
	    (error->request_code == X_ConfigureWindow && error->error_code == BadValue) ||
	    (error->request_code == X_GrabButton && error->error_code == BadAccess) ||
	    (error->request_code == X_GrabKey && error->error_code == BadAccess) ||
	    (error->request_code == X_CopyArea && error->error_code == BadDrawable) ||
	    (error->request_code == 139 && error->error_code == BadDrawable) ||
	    (error->request_code == 139 && error->error_code == 143))
		return 0;

	if (error->request_code < LEN(request_names)) {
		if (error->error_code >= LEN(error_messages))
			error->error_code = 0;
		errx(
			EXIT_FAILURE, "%s: resource 0x%08lX: %s",
			request_names[error->request_code],
			error->resourceid,
			error_messages[error->error_code]
		);
	} else {
		errx(
			EXIT_FAILURE,
			"request #%d: resource 0x%08lX: error %d",
			error->request_code,
			error->resourceid,
			error->error_code
		);
	}
}

static int
xerrorstart(Display *dpy, XErrorEvent *error)
{
	(void)dpy;
	(void)error;
	errx(1, "another window manager is already running");
}

static void
autostart(char *filename)
{
	pid_t pid;

	if (filename == NULL)
		return;
	if ((pid = efork()) == 0) {
		if (efork() == 0)
			execshell(filename);
		exit(0);
	}
	waitpid(pid, NULL, 0);
}

static char *
setoptions(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "AcdhSstW")) != -1) {
		switch (c) {
		case 'A' :
			config.altkeysym = XK_Alt_L;
			config.modifier = Mod1Mask;
			break;
		case 'c':
			config.honorconfig = 1;
			break;
		case 'd':
			config.floatdialog = 1;
			break;
		case 'h':
			config.disablehidden = 1;
			break;
		case 'S':
			config.sloppytiles = 1;
			break;
		case 's':
			config.sloppyfocus = 1;
			break;
		case 't':
			config.disablealttab = 1;
			break;
		case 'W' :
			config.altkeysym = XK_Super_L;
			config.modifier = Mod4Mask;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	return *argv;
}

static void
monupdate(void)
{
	static struct Monitor def_monitor;
	static struct Monitor *list = &def_monitor;
	struct Monitor **monitors = NULL;
	XRRMonitorInfo *infos = NULL;
	int nmonitors = 0;

	def_monitor = (struct Monitor){
		.mx = 0,
		.wx = 0,
		.my = 0,
		.wy = 0,
		.mw = DisplayWidth(dpy, screen),
		.ww = DisplayWidth(dpy, screen),
		.mh = DisplayHeight(dpy, screen),
		.wh = DisplayHeight(dpy, screen),
	};
	infos = XRRGetMonitors(dpy, root, True, &nmonitors);
	if (infos == NULL || nmonitors <= 0)
		goto error;
	monitors = reallocarray(NULL, nmonitors, sizeof(*monitors));
	if (monitors == NULL) {
		monitors = &list;
		nmonitors = 1;
		goto error;
	}
	for (int i = 0; i < nmonitors; i++) {
		monitors[i] = NULL;
		for (int j = 0; j < wm.nmonitors; j++) {
			if (infos[i].name == wm.monitors[j]->name) {
				monitors[i] = wm.monitors[j];
				wm.monitors[j] = NULL;
				break;
			}
		}
		if (monitors[i] == NULL) {
			monitors[i] = emalloc(sizeof(*monitors[i]));
			*monitors[i] = (struct Monitor){
				.mx = infos[i].x,
				.wx = infos[i].x,
				.my = infos[i].y,
				.wy = infos[i].y,
				.mw = infos[i].width,
				.ww = infos[i].width,
				.mh = infos[i].height,
				.wh = infos[i].height,
			};
		}
	}
error:
	XRRFreeMonitors(infos);

	/* delete monitors that do not exist anymore */
	for (int j = 0; j < wm.nmonitors; j++) {
		FOREACH_CLASS(monitor_delete, wm.monitors[j]);
		if (wm.monitors[j] == wm.selmon)
			wm.selmon = monitors[0];
		free(wm.monitors[j]);
	}
	free(wm.monitors);

	/* commit new monitor list */
	wm.monitors = monitors;
	wm.nmonitors = nmonitors;
	FOREACH_CLASS(monitor_reset);
	wm.setclientlist = True;
}

static void
reset_hints(void)
{
	(void)XChangeProperty(
		dpy, root, atoms[_NET_ACTIVE_WINDOW],
		XA_WINDOW, 32, PropModeReplace, (void *)&(Window){None}, 0
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_CLIENT_LIST],
		XA_WINDOW, 32, PropModeReplace, NULL, 0
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_CLIENT_LIST_STACKING],
		XA_WINDOW, 32, PropModeReplace, NULL, 0
	);
	(void)XChangeProperty(
		dpy, root, atoms[_SHOD_CONTAINER_LIST],
		XA_WINDOW, 32, PropModeReplace, NULL, 0
	);
	(void)XChangeProperty(
		dpy, root, atoms[_SHOD_DOCK_LIST],
		XA_WINDOW, 32, PropModeReplace, NULL, 0
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_CURRENT_DESKTOP],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)&(unsigned long){0}, 1
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_SHOWING_DESKTOP],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)&(unsigned long){0}, 1
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_NUMBER_OF_DESKTOPS],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)&config.ndesktops, 1
	);
}

static void
setup(void)
{
	static struct {
		const char *class, *name;
	} resourceids[NRESOURCES] = {
#define X(res, s1, s2) [res] = { .class = s1, .name = s2, },
		RESOURCES
#undef  X
	};
	static char *atomnames[NATOMS] = {
#define X(atom) [atom] = #atom,
		ATOMS
#undef  X
	};
	char const *dpyname;
	XVisualInfo *infos;
	int ninfos;
	struct sigaction sa;

	/* ignore children we inherited and reap zombies we inherited */
	/* we may inherit children if xinitrc execs shod, for example */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		err(1, "sigaction");
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;

	/* setup connection to X server */
	if (!setlocale(LC_ALL, "") || !XSupportsLocale())
		warnx("warning: no locale support for X11");
	if ((dpyname = XDisplayName(NULL)) == NULL || dpyname[0] == '\0')
		errx(EXIT_FAILURE, "DISPLAY is not set");
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(EXIT_FAILURE, "%s: cannot open display", dpyname);
	if (fcntl(XConnectionNumber(dpy), F_SETFD, FD_CLOEXEC) == -1)
		err(EXIT_FAILURE, "connection to display \"%s\"", dpyname);
	if (!XInternAtoms(dpy, atomnames, NATOMS, False, atoms))
		errx(EXIT_FAILURE, "%s: cannot intern atoms", dpyname);
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	context = XUniqueContext();

	/* redirect to us all requests to map/configure top-level windows */
	(void)XSetErrorHandler(xerrorstart);
	(void)XSelectInput(
		/*
		 * XXX: Should we select SubstructureNotifyMask on root window?
		 * We already select StructureNotifyMask on client windows we
		 * are requested to map, so selecting SubstructureNotifyMask on
		 * the root window seems redundant.
		 */
		dpy, root,
		SubstructureRedirectMask |  /* so clients request us to map */
		StructureNotifyMask |       /* get changes on rootwin configuration */
		PropertyChangeMask |        /* get changes on rootwin properties */
		ButtonPressMask             /* to change monitor on mouseclick */
	);
	(void)XSync(dpy, False);
	(void)XSetErrorHandler(xerror);
	(void)XSync(dpy, False);

	visual = NULL;
	infos = XGetVisualInfo(
		dpy, VisualScreenMask | VisualDepthMask | VisualClassMask,
		&(XVisualInfo){
			.screen = screen,
			.depth = 32,
			.class = TrueColor
		}, &ninfos
	);
	for (int i = 0; i < ninfos; i++) {
		XRenderPictFormat *fmt;

		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type != PictTypeDirect)
			continue;
		if (!fmt->direct.alphaMask)
			continue;
		depth = infos[i].depth;
		visual = infos[i].visual;
		colormap = XCreateColormap(dpy, root, visual, AllocNone);
		break;
	}
	XFree(infos);
	if (visual == NULL) {
		depth = DefaultDepth(dpy, screen);
		visual = DefaultVisual(dpy, screen);
		colormap = DefaultColormap(dpy, screen);
	}

	/* init cursors */
	wm.cursors[CURSOR_NORMAL] = XCreateFontCursor(dpy, XC_left_ptr);
	wm.cursors[CURSOR_MOVE] = XCreateFontCursor(dpy, XC_fleur);
	wm.cursors[CURSOR_NW] = XCreateFontCursor(dpy, XC_top_left_corner);
	wm.cursors[CURSOR_NE] = XCreateFontCursor(dpy, XC_top_right_corner);
	wm.cursors[CURSOR_SW] = XCreateFontCursor(dpy, XC_bottom_left_corner);
	wm.cursors[CURSOR_SE] = XCreateFontCursor(dpy, XC_bottom_right_corner);
	wm.cursors[CURSOR_N] = XCreateFontCursor(dpy, XC_top_side);
	wm.cursors[CURSOR_S] = XCreateFontCursor(dpy, XC_bottom_side);
	wm.cursors[CURSOR_W] = XCreateFontCursor(dpy, XC_left_side);
	wm.cursors[CURSOR_E] = XCreateFontCursor(dpy, XC_right_side);
	wm.cursors[CURSOR_V] = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
	wm.cursors[CURSOR_H] = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
	wm.cursors[CURSOR_HAND] = XCreateFontCursor(dpy, XC_hand2);
	wm.cursors[CURSOR_PIRATE] = XCreateFontCursor(dpy, XC_pirate);
	XDefineCursor(dpy, root, wm.cursors[CURSOR_NORMAL]);

	/* init list of monitors */
	wm.monitors = NULL;
	wm.nmonitors = 0;
	monupdate();
	wm.selmon = wm.monitors[0];

	/* create windows used for controlling focus and stack order */
	for (size_t i = 0; i < LEN(wm.layertop); i++) {
		wm.layertop[i] = XCreateSimpleWindow(
			dpy, root, 0, 0, 1, 1, 0, 0, 0
		);
		XRaiseWindow(dpy, wm.layertop[i]);
	}
	wm.checkwin = wm.focuswin = wm.dragwin = wm.restackwin = XCreateWindow(
		dpy, root,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth),
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		0, depth, InputOutput, visual,
		CWEventMask | CWColormap | CWBorderPixel | CWBackPixel,
		&(XSetWindowAttributes){
			.event_mask = MOUSE_EVENTS | KeyPressMask,
			.colormap = colormap,
			.border_pixel = BlackPixel(dpy, screen),
			.background_pixel = BlackPixel(dpy, screen),
		}
	);

	/* set properties declaring that we are EWMH compliant */
	(void)XChangeProperty(
		dpy, wm.checkwin, atoms[_NET_SUPPORTING_WM_CHECK],
		XA_WINDOW, 32, PropModeReplace,
		(void *)&wm.checkwin, 1
	);
	(void)XChangeProperty(
		dpy, wm.checkwin, atoms[_NET_WM_NAME],
		atoms[UTF8_STRING], 8, PropModeReplace,
		(void *)"shod", strlen("shod")
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_SUPPORTING_WM_CHECK],
		XA_WINDOW, 32, PropModeReplace,
		(void *)&wm.checkwin, 1
	);
	(void)XChangeProperty(
		dpy, root, atoms[_NET_SUPPORTED],
		XA_ATOM, 32, PropModeReplace,
		(void *)atoms, NATOMS
	);
	reset_hints();

	/* intern quarks for resources */
	XrmInitialize();
	application.class = XrmPermStringToQuark("Shod");
	application.name = XrmPermStringToQuark("shod");
	anyresource = XrmPermStringToQuark("?");
	for (size_t i = 0; i < NRESOURCES; i++) {
		resources[i].class = XrmPermStringToQuark(resourceids[i].class);
		resources[i].name = XrmPermStringToQuark(resourceids[i].name);
	}
	gc = XCreateGC(
		dpy, wm.dragwin,
		GCFillStyle, &(XGCValues){.fill_style = FillSolid}
	);

	/* initialize theme */
	if (!set_theme())
		exit(EXIT_FAILURE);
	setresources(XResourceManagerString(dpy));

	/* set modifier key and grab alt key */
	setmod();

	/* initialize classes */
	for (size_t i = 0; i < LEN(classes); i++)
		if (classes[i]->init != NULL)
			classes[i]->init();
}

static void
cleanup(void)
{
	XftFontClose(dpy, theme.font);
	for (int i = 0; i < STYLE_LAST; i++)
		for (int j = 0; j < COLOR_LAST; j++)
			XftColorFree(dpy, visual, colormap, &theme.colors[i][j]);
	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, wm.checkwin);
	for (size_t i = 0; i < LEN(wm.layertop); i++)
		XDestroyWindow(dpy, wm.layertop[i]);
	for (size_t i = 0; i < CURSOR_LAST; i++)
		XFreeCursor(dpy, wm.cursors[i]);
	FOREACH_CLASS(clean);
	for (int i = 0; i < wm.nmonitors; i++)
		free(wm.monitors[i]);
	free(wm.monitors);
	reset_hints();
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);
}

void
deskupdate(struct Monitor *mon, long desk)
{
	if (desk < 0 || desk >= config.ndesktops || (mon == wm.selmon && desk == wm.selmon->seldesk))
		return;
	if (wm.showingdesk)
		deskshow(0);
	if (!deskisvisible(mon, desk))
		FOREACH_CLASS(change_desktop, mon, mon->seldesk, desk);
	wm.selmon = mon;
	wm.selmon->seldesk = desk;
	XChangeProperty(
		dpy, root, atoms[_NET_CURRENT_DESKTOP],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)&desk, 1
	);
}

struct Monitor *
getmon(int x, int y)
{
	for (int i = 0; i < wm.nmonitors; i++) {
		if (x < wm.monitors[i]->mx)
			continue;
		if (x >= wm.monitors[i]->mx + wm.monitors[i]->mw)
			continue;
		if (y < wm.monitors[i]->my)
			continue;
		if (y >= wm.monitors[i]->my + wm.monitors[i]->mh)
			continue;
		return wm.monitors[i];
	}
	return NULL;
}

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

void
context_add(XID id, struct Object *data)
{
	if (XSaveContext(dpy, id, context, (void *)data))
		err(EXIT_FAILURE, "cannot save context");
}

void
context_del(XID id)
{
	XDeleteContext(dpy, id, context);
}

struct Object *
context_get(XID id)
{
	XPointer data;

	if (XFindContext(dpy, id, context, &data))
		return NULL;
	return (void *)data;
}

void
window_del(Window window)
{
	context_del(window);
	XDestroyWindow(dpy, window);
}

void
window_close(Display *display, Window window)
{
	XEvent ev;

	ev.type = ClientMessage;
	ev.xclient.window = window;
	ev.xclient.message_type = atoms[WM_PROTOCOLS];
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = atoms[WM_DELETE_WINDOW];
	ev.xclient.data.l[1] = CurrentTime;

	/*
	 * communicate with the given Client, kindly telling it to
	 * close itself and terminate any associated processes using
	 * the WM_DELETE_WINDOW protocol
	 */
	XSendEvent(display, window, False, NoEventMask, &ev);
}

void
winupdatetitle(Window win, char **name)
{
	Atom properties[] = {atoms[_NET_WM_NAME], XA_WM_NAME};

	free(*name);
	for (size_t i = 0; i < LEN(properties); i++) {
		if (getprop(
			dpy, win, properties[i],
			AnyPropertyType, 8, 0,
			(void *)name
		) > 0)
			return;
		XFree(*name);
	}
	*name = NULL;
}

int
main(int argc, char *argv[])
{
	char *filename;
	Bool has_xrandr;
	int screen_change_event;

	filename = setoptions(argc, argv);
	setup();
	autostart(filename);
	scan();
	if ((has_xrandr = XRRQueryExtension(dpy, &screen_change_event, &(int){0})))
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	screen_change_event += RRScreenChangeNotify;
	while (running) {
		XEvent event;
		static void (*event_handlers[LASTEvent])(XEvent *) = {
			[ButtonPress]      = xeventbuttonpress,
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

		XNextEvent(dpy, &event);
		if (event.type < LASTEvent && event_handlers[event.type] != NULL) {
			(*event_handlers[event.type])(&event);
		} else if (has_xrandr && event.type == screen_change_event) {
			if (((XRRScreenChangeNotifyEvent *)&event)->root == root) {
				(void)XRRUpdateConfiguration(&event);
				monupdate();
			}
		}
		if (wm.setclientlist)
			FOREACH_CLASS(list_clients);
		wm.setclientlist = False;
	}
	cleanup();
}
