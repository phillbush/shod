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
#include <strings.h>
#include <unistd.h>

#include <X11/XKBlib.h>
#include <X11/extensions/Xrandr.h>

#include "shod.h"

/* call instance method, if it exists */
#define ARG1(arg, ...) (arg)
#define CALL_METHOD(method, ...) \
	if (ARG1(__VA_ARGS__, 0) != NULL) \
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

struct MwmHints {
	enum {
		MWM_FUNC_ALL            = (1 << 0),
		MWM_FUNC_RESIZE         = (1 << 1),
		MWM_FUNC_MOVE           = (1 << 2),
		MWM_FUNC_MINIMIZE       = (1 << 3),
		MWM_FUNC_MAXIMIZE       = (1 << 4),
		MWM_FUNC_CLOSE          = (1 << 5),
	} functions;

	enum {
		MWM_DECOR_ALL           = (1 << 0),
		MWM_DECOR_BORDER        = (1 << 1),
		MWM_DECOR_RESIZEH       = (1 << 2),
		MWM_DECOR_TITLE         = (1 << 3),
		MWM_DECOR_MENU          = (1 << 4),
		MWM_DECOR_MINIMIZE      = (1 << 5),
		MWM_DECOR_MAXIMIZE      = (1 << 6),
	} decorations;

	enum {
		MWM_TEAROFF_WINDOW      = (1 << 0),
	} status;

	enum {
		MWM_INPUT_MODELESS                      = 0,
		MWM_INPUT_PRIMARY_APPLICATION_MODAL     = 1,
		MWM_INPUT_SYSTEM_MODAL                  = 2,
		MWM_INPUT_FULL_APPLICATION_MODAL        = 3,
	} input_mode;
};

struct GNUHints {
	enum {
		GNU_STYLE_TITLED        = 1,
		GNU_STYLE_CLOSABLE      = 2,
		GNU_STYLE_MINIMIZABLE   = 4,
		GNU_STYLE_RESIZEABLE    = 8,
		GNU_STYLE_ICON          = 64,
		GNU_STYLE_MINIWINDOW    = 128,
	} window_style;

	enum {
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
	} window_level;
};

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
struct Theme theme;

struct WM wm = { 0 };

/* default/hardcoded rules */
struct {
	/* matching class, instance and role */
	const char *class;
	const char *instance;
	const char *role;

	/* type, state, etc to apply on matching windows */
	struct Class *type;
	int state;
	int desktop;
} rules[] = {
	/* CLASS     INSTANCE  ROLE                 TYPE            STATE   DESKTOP*/

	{ "DockApp", NULL,     NULL,                &dockapp_class, 0,      0 },
	/*
	 * Although Firefox/Chrom{e,ium}'s PictureInPicture window is
	 * technically a utility (sub)window, make it a normal one.
	 */
	{ NULL,      NULL,     "PictureInPicture",  &tab_class,     ABOVE,  -1 },
};

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

static enum State
getwinstate(Window win)
{
	enum State statemask = 0x0;
	long nstates = 0;
	Atom *states = NULL;

	if (is_userplaced(win))
		statemask |= USERPLACED;
	nstates = getatomsprop(dpy, win, atoms[_NET_WM_STATE], &states);
	for (long i = 0; i < nstates; i++) {
		if (states[i] == atoms[_NET_WM_STATE_STICKY]) {
			statemask |= STICKY;
		} else if (states[i] == atoms[_NET_WM_STATE_MAXIMIZED_VERT]) {
			statemask |= MAXIMIZED;
		} else if (states[i] == atoms[_NET_WM_STATE_MAXIMIZED_HORZ]) {
			statemask |= MAXIMIZED;
		} else if (states[i] == atoms[_NET_WM_STATE_HIDDEN]) {
			statemask |= MINIMIZED;
		} else if (states[i] == atoms[_NET_WM_STATE_SHADED]) {
			statemask |= SHADED;
		} else if (states[i] == atoms[_NET_WM_STATE_FULLSCREEN]) {
			statemask |= FULLSCREEN;
		} else if (states[i] == atoms[_NET_WM_STATE_ABOVE]) {
			statemask |= ABOVE;
		} else if (states[i] == atoms[_NET_WM_STATE_BELOW]) {
			statemask |= BELOW;
		}
	}
	XFree(states);
	return statemask;
}

#define STRCMP(a, b) ((a) != NULL && (b) != NULL && strcmp((a), (b)) == 0)

static void
manageunknown(struct Object *app, struct Monitor *mon, int desk, Window win,
	Window leader, XRectangle rect, enum State state)
{
	(void)app;
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

static void
deskfocus(struct Monitor *mon, int desk)
{
	if (desk < 0 || desk >= config.ndesktops || (mon == wm.selmon && desk == wm.selmon->seldesk))
		return;
	deskupdate(mon, desk);
	focusnext(mon, desk);
}

static XrmQuark
getquark(const char *str)
{
	if (str != NULL && *str != '\0')
		return XrmStringToQuark(str);
	return anyresource;
}

static struct MwmHints
get_motif_hints(Window window)
{
	enum {
		HAS_FUNCTIONS   = (1 << 0),
		HAS_DECORATIONS = (1 << 1),
		HAS_INPUT_MODE  = (1 << 2),
		HAS_STATUS      = (1 << 3),
	};
	long length = 0;
	long *data = NULL;
	struct MwmHints hints = { 0 };

	length = getprop(
		dpy, window,
		atoms[_MOTIF_WM_HINTS], atoms[_MOTIF_WM_HINTS], 32, 0,
		(void *)&data
	);
	if (length > 1 && FLAG(data[0], HAS_FUNCTIONS))
		hints.functions = data[1];
	if (length > 2 && FLAG(data[0], HAS_DECORATIONS))
		hints.decorations = data[2];
	if (length > 3 && FLAG(data[0], HAS_INPUT_MODE))
		hints.input_mode = data[3];
	if (length > 4 && FLAG(data[0], HAS_STATUS))
		hints.status = data[4];
	XFree(data);
	return hints;
}

static struct GNUHints
get_gnustep_hints(Window window)
{
	enum {
		HAS_WINDOW_STYLE        = (1 << 0),
		HAS_WINDOW_LEVEL        = (1 << 1),
	};
	long length = 0;
	long *data = NULL;
	struct GNUHints hints = { 0 };

	length = getprop(
		dpy, window,
		atoms[_GNUSTEP_WM_ATTR], atoms[_GNUSTEP_WM_ATTR], 32, 0,
		(void *)&data
	);
	if (length > 1 && FLAG(data[0], HAS_WINDOW_STYLE))
		hints.window_style = data[1];
	if (length > 2 && FLAG(data[0], HAS_WINDOW_LEVEL))
		hints.window_level = data[2];
	XFree(data);
	return hints;
}

static void
manage(Window win, Window appwin, XRectangle rect)
{
	enum { I_APP, I_CLASS, I_INSTANCE, I_ROLE, I_RESOURCE, I_NULL, I_LAST };
	static struct Class unknown_class = {
		.setstate       = NULL,
		.manage         = &manageunknown,
		.unmanage       = NULL,
	};
	XrmClass winclass[I_LAST];
	XrmName winname[I_LAST];
	XClassHint classh = { NULL, NULL };
	char *role = NULL;
	char *value;
	struct Object *app;
	struct Class *class = NULL;
	enum State state = getwinstate(win);
	Window dockapp = None;
	Window leader = getwinprop(dpy, win, atoms[WM_CLIENT_LEADER]);
	int desk = wm.selmon->seldesk;

	if (context_get(win) != NULL)
		return;         /* window already managed */

	/* prepare window to be managed */
	XSelectInput(
		dpy, win,
		StructureNotifyMask|PropertyChangeMask
	);
	XSetWindowBorderWidth(dpy, win, 0);

	/* default settings for managed window, overwriten below */
	XGetClassHint(dpy, win, &classh);
	getprop(dpy, win, atoms[WM_WINDOW_ROLE], AnyPropertyType, 8, 0, (void *)&role);
	if (!FLAG(state, USERPLACED))
		rect.x = rect.y = 0;
	app = context_get(appwin);
	if (app != NULL && app->class != &tab_class)
		app = NULL;
	{
		XWMHints *wmhints;

		wmhints = XGetWMHints(dpy, win);
		if (wmhints && FLAG(wmhints->flags, StateHint) &&
		    wmhints->initial_state == WithdrawnState) {
			if (FLAG(wmhints->flags, IconWindowHint))
				dockapp = wmhints->icon_window;
			else
				dockapp = win;
		}
		if (leader == None && wmhints &&
		    FLAG(wmhints->flags, WindowGroupHint)) {
			leader = wmhints->window_group;
		}
		XFree(wmhints);
	}

	/* first try default (hardcoded) rules */
	for (size_t i = 0; i < LEN(rules); i++) {
		if ((rules[i].class == NULL    || STRCMP(rules[i].class, classh.res_class))
		&&  (rules[i].instance == NULL || STRCMP(rules[i].instance, classh.res_name))
		&&  (rules[i].role == NULL     || STRCMP(rules[i].role, role))) {
			if (rules[i].type != NULL)
				class = rules[i].type;
			if (rules[i].state >= 0)
				state = rules[i].state;
			if (rules[i].desktop > 0 && rules[i].desktop <= config.ndesktops)
				desk = rules[i].desktop - 1;
		}
	}

	/* then try the X resource database... */
	winclass[I_APP] = application.class;
	winname[I_APP] = application.name;
	winclass[I_CLASS] = winname[I_CLASS] = getquark(classh.res_class);
	winclass[I_INSTANCE] = winname[I_INSTANCE] = getquark(classh.res_name);
	winclass[I_ROLE] = winname[I_ROLE] = getquark(role);
	winclass[I_NULL] = winname[I_NULL] = NULLQUARK;
	XFree(role);
	XFree(classh.res_class);
	XFree(classh.res_name);

	/* ...for window type */
	winclass[I_RESOURCE] = resources[RES_TYPE].class;
	winname[I_RESOURCE] = resources[RES_TYPE].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL &&
	    strcasecmp(value, "DESKTOP") == 0) {
		class = &dockapp_class;
	}

	/* ...for window state */
	winclass[I_RESOURCE] = resources[RES_STATE].class;
	winname[I_RESOURCE] = resources[RES_STATE].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		state = 0;
		if (strcasestr(value, "above") != NULL)
			state |= ABOVE;
		if (strcasestr(value, "below") != NULL)
			state |= BELOW;
		if (strcasestr(value, "fullscreen") != NULL)
			state |= FULLSCREEN;
		if (strcasestr(value, "maximized") != NULL)
			state |= MAXIMIZED;
		if (strcasestr(value, "minimized") != NULL)
			state |= MINIMIZED;
		if (strcasestr(value, "shaded") != NULL)
			state |= SHADED;
		if (strcasestr(value, "sticky") != NULL)
			state |= STICKY;
		if (strcasestr(value, "extend") != NULL)
			state |= EXTEND;
		if (strcasestr(value, "shrunk") != NULL)
			state |= SHRUNK;
		if (strcasestr(value, "resized") != NULL)
			state |= RESIZED;
	}

	/* ...for dockapp position */
	winclass[I_RESOURCE] = resources[RES_DOCK_POS].class;
	winname[I_RESOURCE] = resources[RES_DOCK_POS].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		long n;
		if ((n = strtol(value, NULL, 10)) >= 0 && n < INT_MAX) {
			rect.x = rect.y = n;
		}
	}

	/* ...for desktop number */
	winclass[I_RESOURCE] = resources[RES_DESKTOP].class;
	winname[I_RESOURCE] = resources[RES_DESKTOP].name;
	if ((value = getresource(xdb, winclass, winname)) != NULL) {
		long n;
		if ((n = strtol(value, NULL, 10)) > 0 && n <= config.ndesktops) {
			desk = n - 1;
		}
	}

	/* guess window type (aka object class, in our OOP vocab) if unknown */
	if (class == NULL) {
		Atom prop = getatomprop(dpy, win, atoms[_NET_WM_WINDOW_TYPE]);
		struct MwmHints mwmhints = get_motif_hints(win);
		struct GNUHints gnuhints = get_gnustep_hints(win);

		if (dockapp != None ||
		    gnuhints.window_style == GNU_STYLE_ICON ||
		    gnuhints.window_style == GNU_STYLE_MINIWINDOW) {
			win = dockapp;
			class = &dockapp_class;
		} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DESKTOP]) {
			state = BELOW;
			class = &unknown_class;
		} else if (prop == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
			class = &notif_class;
		} else if (prop == atoms[_NET_WM_WINDOW_TYPE_PROMPT]) {
			class = &prompt_class;
		} else if (prop == atoms[_NET_WM_WINDOW_TYPE_SPLASH]) {
			class = &splash_class;
		} else if (gnuhints.window_level == GNU_LEVEL_POPUP) {
			state = ABOVE;
			class = &unknown_class;
		} else if (prop == atoms[_NET_WM_WINDOW_TYPE_MENU] ||
			   prop == atoms[_NET_WM_WINDOW_TYPE_UTILITY] ||
			   prop == atoms[_NET_WM_WINDOW_TYPE_TOOLBAR] ||
			   gnuhints.window_level == GNU_LEVEL_PANEL ||
			   gnuhints.window_level == GNU_LEVEL_SUBMENU ||
			   gnuhints.window_level == GNU_LEVEL_MAINMENU ||
			   FLAG(mwmhints.status, MWM_TEAROFF_WINDOW)) {
			class = &menu_class;
		} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DOCK]) {
			class = &bar_class;
		} else if (app != NULL) {
			class = config.floatdialog ? &menu_class : &dialog_class;
		} else {
			class = &tab_class;
		}
	}
	(*class->manage)(app, wm.selmon, desk, win, leader, rect, state);
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
draw_close_btn(int style)
{
	int button_size = max(1, config.titlewidth - config.shadowthickness*2);
	int cross_size = max(1, button_size - config.shadowthickness * 2);

	for (int style = 0; style < STYLE_LAST; style++)
	for (int focus = 0; focus < 2; focus++) {
		unsigned long fg;

		if (focus)
			fg = theme.colors[STYLE_OTHER][COLOR_FG].pixel;
		else
			fg = theme.colors[style][COLOR_FG].pixel;
		updatepixmap(
			&wm.close_btn[style][focus], NULL, NULL,
			config.titlewidth, config.titlewidth
		);
		drawshadow(
			wm.close_btn[style][focus], -config.titlewidth, 0,
			config.titlewidth*2, config.titlewidth, style
		);
		drawshadow(
			wm.close_btn[style][focus],
			config.shadowthickness,
			config.shadowthickness,
			button_size, button_size,
			style
		);
		XChangeGC(dpy, wm.gc, GCForeground|GCLineWidth|GCCapStyle,
			&(XGCValues){
				.foreground = fg,
				.line_width = 2,
				.cap_style = CapProjecting,
			}
		);
		XDrawSegments(dpy, wm.close_btn[style][focus], wm.gc,
			(XSegment[]){
				[0] = {
					.x1 = config.shadowthickness*2 + 1,
					.y1 = config.shadowthickness*2 + 1,
					.x2 = config.shadowthickness + cross_size,
					.y2 = config.shadowthickness + cross_size,
				},
				[1] = {
					.x1 = config.shadowthickness + cross_size,
					.y1 = config.shadowthickness*2 + 1,
					.x2 = config.shadowthickness*2 + 1,
					.y2 = config.shadowthickness + cross_size,
				},
			}, 2
		);
	}
}

static void
reload_theme(void)
{
	Pixmap pix;

	config.corner = config.borderwidth + config.titlewidth;
	config.divwidth = config.borderwidth;
	wm.minsize = config.corner * 2 + 10;
	if (config.borderwidth > 5)
		config.shadowthickness = 2;     /* thick shadow */
	else
		config.shadowthickness = 1;     /* slim shadow */

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
		FOCUSED
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

	for (int style = 0; style < STYLE_LAST; style++)
		draw_close_btn(style);

	FOREACH_CLASS(reload_theme);
}

static void
set_resources(char *xrm)
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
			for (int style = 0; style < STYLE_LAST; style++)
				setcolor(value, style, COLOR_FG);
			break;
		case RES_DOCK_BACKGROUND:
			setcolor(value, STYLE_OTHER, COLOR_BODY);
			break;
		case RES_DOCK_BORDER:
			setcolor(value, STYLE_OTHER, COLOR_DARK);
			break;
		case RES_ACTIVE_BG:
			setcolor(value, FOCUSED, COLOR_BODY);
			break;
		case RES_ACTIVE_TOP:
			setcolor(value, FOCUSED, COLOR_LIGHT);
			break;
		case RES_ACTIVE_BOT:
			setcolor(value, FOCUSED, COLOR_DARK);
			break;
		case RES_INACTIVE_BG:
			setcolor(value, UNFOCUSED, COLOR_BODY);
			break;
		case RES_INACTIVE_TOP:
			setcolor(value, UNFOCUSED, COLOR_LIGHT);
			break;
		case RES_INACTIVE_BOT:
			setcolor(value, UNFOCUSED, COLOR_DARK);
			break;
		case RES_URGENT_BG:
			setcolor(value, URGENT, COLOR_BODY);
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
	reload_theme();
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
	reload_theme();
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

	XAllowEvents(dpy, ReplayPointer, CurrentTime);
	(void)XSync(dpy, False);

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
	} else if (press->window == root && mon != NULL && (
		press->button == Button1 ||
		press->button == Button2 ||
		press->button == Button3
	)) {
		deskfocus(mon, mon->seldesk);
	}
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
xeventfocusin(XEvent *event)
{
	if (event->xfocus.window == root)
		CALL_METHOD(focus, wm.focused);
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
	XMapRequestEvent *mapping;
	XWindowAttributes wa;
	Window appwin = None;

	mapping = &e->xmaprequest;
	if (!XGetWindowAttributes(dpy, mapping->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	if (!XGetTransientForHint(dpy, mapping->window, &appwin))
		appwin = None;
	manage(
		mapping->window, appwin,
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

	if (event->state != PropertyNewValue)
		return;
	if (event->window == root && event->atom == XA_RESOURCE_MANAGER) {
		char *str = NULL;
		getprop(
			dpy, root, XA_RESOURCE_MANAGER, AnyPropertyType, 8,
			0, (void *)&str
		);
		if (str == NULL)
			return;
		XrmDestroyDatabase(xdb);
		set_resources(str);
		XFree(str);
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

static void
scan(void)
{
	Window appwin;
	Window *toplvls = NULL;
	unsigned int ntoplvls = 0;
	XWindowAttributes wa;

	/* no new window should be created while we scan for windows to manage */
	XGrabServer(dpy);

	if (!XQueryTree(
		dpy, root,
		&(Window){0}, &(Window){0},
		&toplvls, &ntoplvls)
	) ntoplvls = 0;

	/* first manage main/leader windows */
	for (unsigned int i = 0; i < ntoplvls; i++) {
		appwin = None;
		if (!XGetWindowAttributes(dpy, toplvls[i], &wa))
			continue;
		if (wa.override_redirect || wa.map_state != IsViewable)
			continue;
		if (XGetTransientForHint(dpy, toplvls[i], &appwin) && appwin != None)
			continue;
		manage(toplvls[i], None, (XRectangle){
			.x = wa.x,
			.y = wa.y,
			.width = wa.width,
			.height = wa.height,
		});
	}

	/* now manage transient/dialog windows */
	for (unsigned int i = 0; i < ntoplvls; i++) {
		appwin = None;
		if (!XGetWindowAttributes(dpy, toplvls[i], &wa))
			continue;
		if (wa.override_redirect || wa.map_state != IsViewable)
			continue;
		if (!XGetTransientForHint(dpy, toplvls[i], &appwin) || appwin == None)
			continue;
		manage(toplvls[i], appwin, (XRectangle){
			.x = wa.x,
			.y = wa.y,
			.width = wa.width,
			.height = wa.height,
		});
	}

	/*
	 * The focus-holding window is mapped after managing already mapped
	 * windows for it to not get managed.
	 */
	XMapWindow(dpy, wm.focuswin);
	XSetInputFocus(dpy, wm.focuswin, RevertToPointerRoot, CurrentTime);

	XFree(toplvls);
	XSync(dpy, True);
	XUngrabServer(dpy);
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
	if (filename == NULL)
		return;
	switch (fork()) {
	case -1:	err(1, "fork");		break;
	case 0:		execshell(filename);	break;
	}
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
	static struct Monitor def_monitor = {0};
	static struct Monitor *list = &def_monitor;
	struct Monitor **monitors = NULL;
	XRRMonitorInfo *infos = NULL;
	int nmonitors = 0;

	def_monitor.geometry = def_monitor.window_area = (XRectangle){
		.x = 0,
		.y = 0,
		.width = DisplayWidth(dpy, screen),
		.height = DisplayHeight(dpy, screen),
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
			monitors[i] = ecalloc(1, sizeof(*monitors[i]));
			monitors[i]->geometry = monitors[i]->window_area = (XRectangle){
				.x = infos[i].x,
				.y = infos[i].y,
				.width = infos[i].width,
				.height = infos[i].height,
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
		ButtonPressMask |           /* to change monitor on mouseclick */
		FocusChangeMask
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
			.event_mask = StructureNotifyMask | MOUSE_EVENTS | KeyPressMask,
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
	wm.gc = XCreateGC(
		dpy, wm.dragwin,
		GCFillStyle, &(XGCValues){.fill_style = FillSolid}
	);

	if (!set_theme())
		exit(EXIT_FAILURE);
	set_resources(XResourceManagerString(dpy));

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
	for (int style = 0; style < STYLE_LAST; style++) {
		for (int focus = 0; focus < 2; focus++)
			XFreePixmap(dpy, wm.close_btn[style][focus]);
		for (int color = 0; color < COLOR_LAST; color++) {
			XftColorFree(
				dpy, visual, colormap,
				&theme.colors[style][color]
			);
		}
	}
	XFreeGC(dpy, wm.gc);
	XDestroyWindow(dpy, wm.checkwin);
	for (size_t layer = 0; layer < LEN(wm.layertop); layer++)
		XDestroyWindow(dpy, wm.layertop[layer]);
	for (size_t cursor = 0; cursor < CURSOR_LAST; cursor++)
		XFreeCursor(dpy, wm.cursors[cursor]);
	FOREACH_CLASS(clean);
	for (int mon = 0; mon < wm.nmonitors; mon++)
		free(wm.monitors[mon]);
	for (int mon = 0; mon < wm.nmonitors; mon++)
		free(wm.monitors[mon]);
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
		if (x < wm.monitors[i]->geometry.x)
			continue;
		if (x >= wm.monitors[i]->geometry.x + wm.monitors[i]->geometry.width)
			continue;
		if (y < wm.monitors[i]->geometry.y)
			continue;
		if (y >= wm.monitors[i]->geometry.y + wm.monitors[i]->geometry.height)
			continue;
		return wm.monitors[i];
	}
	return NULL;
}

void
fitmonitor(struct Monitor *mon, XRectangle *geometry, float factor)
{
	int origw, origh;
	int minw, minh;

	origw = geometry->width;
	origh = geometry->height;
	minw = min(origw, mon->window_area.width * factor);
	minh = min(origh, mon->window_area.height * factor);
	if (origw * minh > origh * minw) {
		minh = (origh * minw) / origw;
		minw = (origw * minh) / origh;
	} else {
		minw = (origw * minh) / origh;
		minh = (origh * minw) / origw;
	}
	geometry->width = max(wm.minsize, minw);
	geometry->height = max(wm.minsize, minh);
	geometry->x = max(mon->window_area.x, min(mon->window_area.x + mon->window_area.width - geometry->width, geometry->x));
	geometry->y = max(mon->window_area.y, min(mon->window_area.y + mon->window_area.height - geometry->height, geometry->y));
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
window_close(Display *display, Window window)
{
	/*
	 * communicate with the given Client, kindly telling it to
	 * close itself and terminate any associated processes using
	 * the WM_DELETE_WINDOW protocol
	 */
	XSendEvent(
		display, window, False,
		NoEventMask, (XEvent *)&(XClientMessageEvent){
			.type = ClientMessage,
			.serial = 0,
			.send_event = True,
			.message_type = atoms[WM_PROTOCOLS],
			.window = window,
			.format = 32,
			.data.l[0] = atoms[WM_DELETE_WINDOW],
			.data.l[1] = CurrentTime,
			.data.l[2] = 0,
			.data.l[3] = 0,
			.data.l[4] = 0,
		}
	);
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

void
deskshow(Bool show)
{
	if (wm.showingdesk && show)
		return;
	if (!wm.showingdesk && !show)
		return;
	if (show)
		FOREACH_CLASS(show_desktop);
	else
		FOREACH_CLASS(hide_desktop);
	wm.showingdesk = show;
	XChangeProperty(
		dpy, root, atoms[_NET_SHOWING_DESKTOP],
		XA_CARDINAL, 32, PropModeReplace,
		(void *)&show, 1
	);
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
	if ((has_xrandr = XRRQueryExtension(dpy, &screen_change_event, &(int){0})))
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	screen_change_event += RRScreenChangeNotify;
	scan();
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
