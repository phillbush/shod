#include <sys/wait.h>

#include <err.h>
#include <errno.h>
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

#define _SHOD_MOVERESIZE_RELATIVE       ((long)(1 << 16))
#define MOUSEEVENTMASK          (ButtonReleaseMask | PointerMotionMask)
#define BETWEEN(a, b, c)        ((a) <= (b) && (b) < (c))

/* call object method, if it exists */
#define ARG1(arg, ...) (arg)
#define MESSAGE(method, ...) \
	if (ARG1(__VA_ARGS__, 0)->class->method != NULL) \
		ARG1(__VA_ARGS__, 0)->class->method(__VA_ARGS__)
#define METHOD(method, ...) \
	for (size_t i = 0; i < LEN(classes); i++) \
		if (classes[i]->method != NULL) \
			classes[i]->method(__VA_ARGS__)
#define unmanage(...) MESSAGE(unmanage, __VA_ARGS__)
#define btnpress(...) MESSAGE(btnpress, __VA_ARGS__)
#define handle_property(...) MESSAGE(handle_property, __VA_ARGS__)
#define handle_message(...) MESSAGE(handle_message, __VA_ARGS__)
#define handle_configure(...) MESSAGE(handle_configure, __VA_ARGS__)
#define handle_enter(...) MESSAGE(handle_enter, __VA_ARGS__)
#define show_desktop(...) METHOD(show_desktop, __VA_ARGS__)
#define hide_desktop(...) METHOD(hide_desktop, __VA_ARGS__)
#define change_desktop(...) METHOD(change_desktop, __VA_ARGS__)
#define redecorate_all(...) METHOD(redecorate_all, __VA_ARGS__)

static int (*xerrorxlib)(Display *, XErrorEvent *);
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
static struct Class unknown_class = {
	.setstate       = NULL,
	.manage         = manageunknown,
	.unmanage       = NULL,
};

static KeyCode altkey;
static KeyCode tabkey;

unsigned long clientmask = CWEventMask | CWColormap | CWBackPixel | CWBorderPixel;
XSetWindowAttributes clientswa = {
	.event_mask = StructureNotifyMask | SubstructureRedirectMask
	            | ButtonReleaseMask | ButtonPressMask
	            | FocusChangeMask | Button1MotionMask
};
struct WM wm = { .running = 1 };
struct Dock dock;
XContext context;

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
		obj = context_get(tmpwin);
		if (obj == NULL)
			return NULL;
		if (obj->class != &tab_class)
			return NULL;
		return (struct Tab *)obj;
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
		class = &dockapp_class;
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
			*tab = gettabfrompid(getcardprop(win, atoms[_NET_WM_PID]));
		class = &tab_class;
	}

done:
	if (class == &dockapp_class)
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

/* (un)show desktop */
static void
deskshow(int show)
{
	if (show) {
		show_desktop();
	} else {
		hide_desktop();
	}
	wm.showingdesk = show;
	ewmhsetshowingdesktop(show);
	menuupdate();
}

/* update desktop */
void
deskupdate(struct Monitor *mon, int desk)
{
	if (desk < 0 || desk >= config.ndesktops || (mon == wm.selmon && desk == wm.selmon->seldesk))
		return;
	if (wm.showingdesk)
		deskshow(0);
	if (!deskisvisible(mon, desk))
		change_desktop(mon, mon->seldesk, desk);
	wm.selmon = mon;
	wm.selmon->seldesk = desk;
	ewmhsetcurrentdesktop(desk);
}

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

	if (context_get(win) != NULL)
		return;
	preparewin(win);
	class = getwinclass(win, &leader, &tab, &state, &rect, &desk);
	(*class->manage)(tab, wm.selmon, desk, win, leader, rect, state);
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
		btnpress(obj, press);
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
		message->window = wm.focused->obj.win;
	}
	if (message->message_type == atoms[_NET_CURRENT_DESKTOP])
		deskfocus(wm.selmon, message->data.l[0]);
	else if (message->message_type == atoms[_SHOD_CYCLE])
		alttab(altkey, tabkey, message->data.l[0]);
	else if (message->message_type == atoms[_NET_SHOWING_DESKTOP])
		deskshow(message->data.l[0]);
	else if (message->message_type == atoms[_NET_CLOSE_WINDOW])
		window_close(message->window);
	else if ((obj = context_get(message->window)) != NULL &&
	         obj->win == message->window)
		handle_message(obj, message->message_type, message->data.l);
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
		handle_configure(obj, configure->value_mask, &changes);
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
		wm.running = False;
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
	unmanage(obj);
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
	handle_enter(obj);
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
		redecorate_all();
		dockreset();
	} else if (
		(obj = context_get(event->window)) != NULL &&
		event->window == obj->win
	) {
		handle_property(obj, event->atom);
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
xerror(Display *dpy, XErrorEvent *e)
{
	/* stolen from berry, which stole from katriawm, which stole from dwm lol */

	/* There's no way to check accesses to destroyed windows, thus those
	 * cases are ignored (especially on UnmapNotify's). Other types of
	 * errors call Xlibs default error handler, which may call exit. */
	if (e->error_code == BadWindow ||
	    (e->request_code == X_SetInputFocus && e->error_code == BadMatch) ||
	    (e->request_code == X_PolyText8 && e->error_code == BadDrawable) ||
	    (e->request_code == X_PolyFillRectangle && e->error_code == BadDrawable) ||
	    (e->request_code == X_PolySegment && e->error_code == BadDrawable) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadMatch) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadValue) ||
	    (e->request_code == X_GrabButton && e->error_code == BadAccess) ||
	    (e->request_code == X_GrabKey && e->error_code == BadAccess) ||
	    (e->request_code == X_CopyArea && e->error_code == BadDrawable) ||
	    (e->request_code == 139 && e->error_code == BadDrawable) ||
	    (e->request_code == 139 && e->error_code == 143))
		return 0;

	fprintf(stderr, "shod: ");
	return xerrorxlib(dpy, e);
	exit(1);        /* unreached */
}

/* startup error handler to check if another window manager is already running */
static int
xerrorstart(Display *dpy, XErrorEvent *e)
{
	(void)dpy;
	(void)e;
	errx(1, "another window manager is already running");
}

/* read command-line options */
static char *
getoptions(int argc, char *argv[])
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

/* initialize signals */
static void
initsignal(void)
{
	struct sigaction sa;

	/* remove zombies, we may inherit children when exec'ing shod in .xinitrc */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		err(1, "sigaction");
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

/* initialize cursors */
static void
initcursors(void)
{
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
}

/* create dummy windows used for controlling focus and the layer of clients */
static void
initdummywindows(void)
{
	int i;
	XSetWindowAttributes swa;

	for (i = 0; i < LAYER_LAST; i++) {
		wm.layers[i].ncols = 0;
		wm.layers[i].obj.win = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
		XRaiseWindow(dpy, wm.layers[i].obj.win);
		TAILQ_INSERT_HEAD(&wm.stackq, &wm.layers[i], raiseentry);
	}
	swa = clientswa;
	swa.event_mask |= KeyPressMask;
	wm.checkwin = wm.focuswin = wm.dragwin = wm.restackwin = XCreateWindow(
		dpy, root,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth),
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		0, depth, CopyFromParent, visual,
		clientmask,
		&swa
	);
}

/* map and hide dummy windows */
static void
mapdummywins(void)
{
	XMapWindow(dpy, wm.focuswin);
	XSetInputFocus(dpy, wm.focuswin, RevertToParent, CurrentTime);
}

/* run stdin on sh */
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

static void
checkotherwm(void)
{
	/*
	 * NOTE: Do we need to select SubstructureNotifyMask on the root window?
	 *
	 * We will always select StructureNotifyMask on the client windows we
	 * are requested to map, so selecting StructureNotifyMask on the root
	 * window seems redundant.
	 *
	 * I have removed this mask in the bitmask passed as the third parameter
	 * to XSelectInput(3) down here.  If anything ever brake, just add it
	 * back.
	 */
	xerrorxlib = XSetErrorHandler(xerrorstart);
	XSelectInput(
		dpy, root,
		SubstructureRedirectMask |      /* so clients request us to map */
		StructureNotifyMask |           /* get changes on root configuration */
		PropertyChangeMask |            /* get changes on root properties */
		ButtonPressMask                 /* to change monitor when clicking */
	);
	XSync(dpy, False);
	(void)XSetErrorHandler(xerror);
	XSync(dpy, False);
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
	return (struct Object *)data;
}

void
window_del(Window window)
{
	context_del(window);
	XDestroyWindow(dpy, window);
}

void
moninit(void)
{
	int i;

	if ((wm.xrandr = XRRQueryExtension(dpy, &wm.xrandrev, &i)))
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask);
	wm.xrandrev += RRScreenChangeNotify;
}

void
mondel(struct Monitor *mon)
{
	TAILQ_REMOVE(&wm.monq, mon, entry);
	for (size_t i = 0; i < LEN(classes); i++)
		if (classes[i]->monitor_delete != NULL)
			classes[i]->monitor_delete(mon);
	free(mon);
}

struct Monitor *
getmon(int x, int y)
{
	struct Monitor *mon;

	TAILQ_FOREACH(mon, &wm.monq, entry)
		if (x >= mon->mx && x < mon->mx + mon->mw && y >= mon->my && y < mon->my + mon->mh)
			return mon;
	return NULL;
}

void
monupdate(void)
{
	XRRScreenResources *sr;
	XRRCrtcInfo *ci;
	struct MonitorQueue monq;               /* queue of monitors */
	struct Monitor *mon;
	int delselmon, i;

	TAILQ_INIT(&monq);
	sr = XRRGetScreenResources(dpy, root);
	for (i = 0, ci = NULL; i < sr->ncrtc; i++) {
		if ((ci = XRRGetCrtcInfo(dpy, sr, sr->crtcs[i])) == NULL)
			continue;
		if (ci->noutput == 0)
			goto next;

		TAILQ_FOREACH(mon, &wm.monq, entry)
			if (ci->x == mon->mx && ci->y == mon->my &&
			    (int)ci->width == mon->mw && (int)ci->height == mon->mh)
				break;
		if (mon != NULL) {
			TAILQ_REMOVE(&wm.monq, mon, entry);
		} else {
			mon = emalloc(sizeof(*mon));
			*mon = (struct Monitor){
				.mx = ci->x,
				.wx = ci->x,
				.my = ci->y,
				.wy = ci->y,
				.mw = ci->width,
				.ww = ci->width,
				.mh = ci->height,
				.wh = ci->height,
			};
		}
		TAILQ_INSERT_TAIL(&monq, mon, entry);
next:
		XRRFreeCrtcInfo(ci);
	}
	XRRFreeScreenResources(sr);

	/* delete monitors that do not exist anymore */
	delselmon = 0;
	while ((mon = TAILQ_FIRST(&wm.monq)) != NULL) {
		if (mon == wm.selmon) {
			delselmon = 1;
			wm.selmon = NULL;
		}
		mondel(mon);
	}
	if (delselmon) {
		wm.selmon = TAILQ_FIRST(&monq);
	}

	/* commit new list of monitor */
	while ((mon = TAILQ_FIRST(&monq)) != NULL) {
		TAILQ_REMOVE(&monq, mon, entry);
		TAILQ_INSERT_TAIL(&wm.monq, mon, entry);
	}

	for (size_t i = 0; i < LEN(classes); i++)
		if (classes[i]->monitor_reset != NULL)
			classes[i]->monitor_reset();
	wm.setclientlist = True;
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

int
main(int argc, char *argv[])
{
	char *filename, *wmname;

	if (!setlocale(LC_ALL, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	xinit();
	context = XUniqueContext();
	checkotherwm();
	moninit();
	initdepth();
	clientswa.colormap = colormap;
	clientswa.border_pixel = BlackPixel(dpy, screen);
	clientswa.background_pixel = BlackPixel(dpy, screen);

	if ((wmname = strrchr(argv[0], '/')) != NULL)
		wmname++;
	else if (argv[0] != NULL && argv[0][0] != '\0')
		wmname = argv[0];
	else
		wmname = "shod";
	filename = getoptions(argc, argv);

	/* check sloppy focus */
	if (config.sloppyfocus || config.sloppytiles)
		clientswa.event_mask |= EnterWindowMask;

	/* initialize */
	TAILQ_INIT(&wm.monq);
	TAILQ_INIT(&wm.stackq);
	initsignal();
	initcursors();
	initatoms();
	initdummywindows();
	inittheme();
	for (size_t i = 0; i < LEN(classes); i++)
		if (classes[i]->init != NULL)
			classes[i]->init();

	/* set up list of monitors */
	monupdate();
	wm.selmon = TAILQ_FIRST(&wm.monq);

	/* initialize ewmh hints */
	ewmhinit(wmname);
	ewmhsetcurrentdesktop(0);
	ewmhsetshowingdesktop(0);
	ewmhsetclients();
	ewmhsetactivewindow(None);

	/* run sh script */
	autostart(filename);

	/* scan windows */
	scan();
	mapdummywins();

	/* change default cursor */
	XDefineCursor(dpy, root, wm.cursors[CURSOR_NORMAL]);

	/* Set focus to root window */
	XSetInputFocus(dpy, root, RevertToParent, CurrentTime);

	/* set modifier key and grab alt key */
	setmod();

	while (wm.running) {
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

		(void)XNextEvent(dpy, &event);
		if (event.type < LASTEvent && event_handlers[event.type] != NULL) {
			(*event_handlers[event.type])(&event);
		} else if (wm.xrandr && event.type == wm.xrandrev) {
			if (((XRRScreenChangeNotifyEvent *)&event)->root == root) {
				(void)XRRUpdateConfiguration(&event);
				monupdate();
			}
		}
		if (wm.setclientlist)
			ewmhsetclients();
		wm.setclientlist = False;
	}

	cleantheme();
	XDestroyWindow(dpy, wm.checkwin);
	for (size_t i = 0; i < LAYER_LAST; i++)
		XDestroyWindow(dpy, wm.layers[i].obj.win);
	for (size_t i = 0; i < CURSOR_LAST; i++)
		XFreeCursor(dpy, wm.cursors[i]);
	for (size_t i = 0; i < LEN(classes); i++)
		if (classes[i]->clean != NULL)
			classes[i]->clean();
	for (struct Monitor *mon = TAILQ_FIRST(&wm.monq);
	     mon != NULL;
	     mon = TAILQ_FIRST(&wm.monq)
	)
		mondel(mon);

	/* clear ewmh hints */
	ewmhsetclients();
	ewmhsetactivewindow(None);

	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);
}
