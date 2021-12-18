#include <err.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/xpm.h>
#include <X11/extensions/Xinerama.h>

#define MODIFIER                Mod1Mask
#define DIV                     15      /* see containerplace() for details */
#define IGNOREUNMAP             6       /* number of unmap notifies to ignore while scanning existing clients */
#define NAMEMAXLEN              1024    /* maximum length of window's name */
#define DROPPIXELS              30      /* number of pixels from the border where a tab can be dropped in */
#define RESIZETIME              64      /* time to redraw containers during resizing */
#define _SHOD_MOVERESIZE_RELATIVE       ((long)(1 << 16))

/* window type */
enum {
	TYPE_NORMAL,
	TYPE_DESKTOP,
	TYPE_DOCK,
	TYPE_NOTIFICATION,
	TYPE_PROMPT,
	TYPE_DOCKAPP
};

/* title bar buttons */
enum {
	BUTTON_NONE,
	BUTTON_LEFT,
	BUTTON_RIGHT
};

/* state action */
enum {
	REMOVE = 0,
	ADD    = 1,
	TOGGLE = 2
};

/* colors */
enum {
	COLOR_MID,
	COLOR_LIGHT,
	COLOR_DARK,
	COLOR_LAST
};

/* decoration style */
enum {
	FOCUSED,
	UNFOCUSED,
	URGENT,
	STYLE_LAST
};

/* decoration state */
enum {
	/* the first decoration state is used both for focused tab and for unpressed borders */
	UNPRESSED     = 0,
	TAB_FOCUSED   = 0,

	PRESSED       = 1,
	TAB_PRESSED   = 1,

	/* the third decoration state is used for unfocused tab and dialog borders */
	TAB_UNFOCUSED = 2,
	DIALOG        = 2,
	NOTIFICATION  = 2,
	DIVISION      = 2,

	DECOR_LAST    = 3
};

/* cursor types */
enum {
	CURSOR_NORMAL,
	CURSOR_MOVE,
	CURSOR_NW,
	CURSOR_NE,
	CURSOR_SW,
	CURSOR_SE,
	CURSOR_N,
	CURSOR_S,
	CURSOR_W,
	CURSOR_E,
	CURSOR_V,
	CURSOR_H,
	CURSOR_PIRATE,
	CURSOR_LAST
};

/* window layers */
enum {
	LAYER_DESKTOP,
	LAYER_BELOW,
	LAYER_NORMAL,
	LAYER_ABOVE,
	LAYER_DOCK,
	LAYER_FULLSCREEN,
	LAYER_LAST
};

/* atoms */
enum {
	/* utf8 */
	UTF8_STRING,

	/* ICCCM atoms */
	WM_DELETE_WINDOW,
	WM_WINDOW_ROLE,
	WM_TAKE_FOCUS,
	WM_PROTOCOLS,
	WM_STATE,

	/* EWMH atoms */
	_NET_SUPPORTED,
	_NET_CLIENT_LIST,
	_NET_CLIENT_LIST_STACKING,
	_NET_NUMBER_OF_DESKTOPS,
	_NET_CURRENT_DESKTOP,
	_NET_ACTIVE_WINDOW,
	_NET_WM_DESKTOP,
	_NET_SUPPORTING_WM_CHECK,
	_NET_SHOWING_DESKTOP,
	_NET_CLOSE_WINDOW,
	_NET_MOVERESIZE_WINDOW,
	_NET_WM_MOVERESIZE,
	_NET_WM_NAME,
	_NET_WM_WINDOW_TYPE,
	_NET_WM_WINDOW_TYPE_DESKTOP,
	_NET_WM_WINDOW_TYPE_MENU,
	_NET_WM_WINDOW_TYPE_TOOLBAR,
	_NET_WM_WINDOW_TYPE_DOCK,
	_NET_WM_WINDOW_TYPE_DIALOG,
	_NET_WM_WINDOW_TYPE_UTILITY,
	_NET_WM_WINDOW_TYPE_SPLASH,
	_NET_WM_WINDOW_TYPE_PROMPT,
	_NET_WM_WINDOW_TYPE_NOTIFICATION,
	_NET_WM_STATE,
	_NET_WM_STATE_STICKY,
	_NET_WM_STATE_MAXIMIZED_VERT,
	_NET_WM_STATE_MAXIMIZED_HORZ,
	_NET_WM_STATE_SHADED,
	_NET_WM_STATE_HIDDEN,
	_NET_WM_STATE_FULLSCREEN,
	_NET_WM_STATE_ABOVE,
	_NET_WM_STATE_BELOW,
	_NET_WM_STATE_FOCUSED,
	_NET_WM_STATE_DEMANDS_ATTENTION,
	_NET_WM_STRUT,
	_NET_WM_STRUT_PARTIAL,
	_NET_REQUEST_FRAME_EXTENTS,
	_NET_FRAME_EXTENTS,

	/* shod atoms */
	_SHOD_GROUP_TAB,
	_SHOD_GROUP_CONTAINER,

	ATOM_LAST
};

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

/* strut elements */
enum {
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

/* border */
enum {
	BORDER_N,
	BORDER_S,
	BORDER_W,
	BORDER_E,
	BORDER_NW,
	BORDER_NE,
	BORDER_SW,
	BORDER_SE,
	BORDER_LAST
};

/* window eight sections (aka octants) */
enum Octant {
	C  = 0,
	N  = (1 << 0),
	S  = (1 << 1),
	W  = (1 << 2),
	E  = (1 << 3),
	NW = (1 << 0) | (1 << 2),
	NE = (1 << 0) | (1 << 3),
	SW = (1 << 1) | (1 << 2),
	SE = (1 << 1) | (1 << 3),
};

/* struct returned by getwindow */
struct Winres {
	struct Dock *dock;              /* dock of window */
	struct Dockapp *dapp;           /* dockapp of window */
	struct Bar *bar;                /* bar of window */
	struct Notification *n;         /* notification of window */
	struct Container *c;            /* container of window */
	struct Row *row;                /* row (with buttons) of window */
	struct Tab *t;                  /* tab of window */
	struct Dialog *d;               /* dialog of window */
};

/* dialog window structure */
struct Dialog {
	struct Dialog *prev, *next;             /* pointer for list of dialogs */
	struct Tab *t;                          /* pointer to parent tab */
	Window frame;                           /* window to reparent the client window */
	Window win;                             /* actual client window */
	Pixmap pix;                             /* pixmap to draw the frame */
	int x, y, w, h;                         /* geometry of the dialog inside the tab frame */
	int maxw, maxh;                         /* maximum size of the dialog */
	int pw, ph;                             /* pixmap size */
	int ignoreunmap;                        /* number of unmapnotifys to ignore */
};

/* tab structure */
struct Tab {
	struct Tab *prev, *next;                /* pointer for list of tabs */
	struct Row *row;                        /* pointer to parent row */
	struct Dialog *ds;                      /* pointer to list of dialogs */
	Window title;                           /* title bar */
	Window win;                             /* actual client window */
	Window frame;                           /* window to reparent the client window */
	Pixmap pix;                             /* pixmap to draw the background of the frame */
	Pixmap pixtitle;                        /* pixmap to draw the background of the title window */
	char *name;                             /* client name */
	int ignoreunmap;                        /* number of unmapnotifys to ignore */
	int isurgent;                           /* whether tab is urgent */
	int winw, winh;                         /* window geometry */
	int x, w;                               /* tab geometry */
	int ptw;                                /* pixmap width for the title window */
	int pw, ph;                             /* pixmap size of the frame */
};

/* row of tiled container */
struct Row {
	struct Row *prev, *next;                /* pointer for list of rows */
	struct Column *col;                     /* pointer to parent column */
	struct Tab *tabs;                       /* list of tabs */
	struct Tab *seltab;                     /* pointer to selected tab */
	Window div;                             /* horizontal division between rows */
	Window frame;                           /* where tab frames are */
	Window bar;                             /* title bar frame */
	Window bl;                              /* left button */
	Window br;                              /* right button */
	Pixmap pixbar;                          /* pixmap for the title bar */
	Pixmap pixbl;                           /* pixmap for left button */
	Pixmap pixbr;                           /* pixmap for right button */
	double fact;                            /* factor of height relative to container */
	int ntabs;                              /* number of tabs */
	int y, h;                               /* row geometry */
	int pw;
};

/* column of tiled container */
struct Column {
	struct Column *prev, *next;             /* pointers for list of columns */
	struct Container *c;                    /* pointer to parent container */
	struct Row *rows;                       /* list of rows */
	struct Row *selrow;                     /* pointer to selected row */
	struct Row *maxrow;                     /* maximized row */
	Window div;                             /* vertical division between columns */
	double fact;                            /* factor of width relative to container */
	int nrows;                              /* number of rows */
	int x, w;                               /* column geometry */
};

/* container structure */
struct Container {
	struct Container *prev, *next;          /* pointers for list of containers */
	struct Container *fprev, *fnext;        /* pointers for list of containers in decrescent focus order */
	struct Container *rprev, *rnext;        /* pointers for list of containers from raised to lowered */
	struct Monitor *mon;                    /* monitor container is on */
	struct Desktop *desk;                   /* desktop container is on */
	struct Column *cols;                    /* list of columns in container */
	struct Column *selcol;                  /* pointer to selected container */
	Window curswin[BORDER_LAST];            /* dummy window used for change cursor while hovering borders */
	Window frame;                           /* window to reparent the contents of the container */
	Pixmap pix;                             /* pixmap to draw the frame */
	int ncols;                              /* number of columns */
	int ismaximized, issticky;              /* window states */
	int isminimized, isshaded;              /* window states */
	int isfullscreen;                       /* whether container is fullscreen */
	int ishidden;                           /* whether container is hidden */
	int layer;                              /* stacking order */
	int x, y, w, h, b;                      /* current geometry */
	int pw, ph;                             /* pixmap width and height */
	int nx, ny, nw, nh;                     /* non-maximized geometry */
};

/* desktop of a monitor */
struct Desktop {
	struct Monitor *mon;                    /* monitor the desktop is in */
	int n;                                  /* desktop number */
};

/* bar (aka dock or panel) */
struct Bar {
	struct Bar *prev, *next;                /* pointers for list of bars */
	Window win;                             /* bar window */
	int strut[STRUT_LAST];                  /* strut values */
	int partial;                            /* strut has 12 elements rather than 4 */
};

/* docked application */
struct Dockapp {
	struct Dockapp *prev, *next;
	Window win;                     /* dockapp window */
	int x, y, w, h;                 /* dockapp position and size */
	int gw, gh;                     /* size of dockapp grid */
	int ignoreunmap;                /* number of unmap requests to ignore */
};

/* data of a monitor */
struct Monitor {
	struct Monitor *prev, *next;            /* pointers for list of monitors */
	struct Desktop *desks;                  /* array of desktops */
	struct Desktop *seldesk;                /* pointer to focused desktop on that monitor */
	int mx, my, mw, mh;                     /* monitor size */
	int wx, wy, ww, wh;                     /* window area */
	int n;                                  /* monitor number */
};

/* notification structure */
struct Notification {
	struct Notification *prev, *next;       /* pointers for list of notifications */
	Window frame;                           /* window to reparent the actual client window */
	Window win;                             /* actual client window */
	Pixmap pix;                             /* pixmap to draw the frame */
	int w, h;                               /* geometry of the entire thing (content + decoration) */
	int pw, ph;                             /* pixmap width and height */
};

/* prompt structure, used only when calling promptisvalid() */
struct Prompt {
	Window win, frame;                      /* actual client window and its frame */
	Pixmap pix;                             /* pixmap to draw the frame */
	int pw, ph;                             /* pixmap width and height */
};

/* cursors, fonts, decorations, and decoration sizes */
struct Visual {
	Cursor cursors[CURSOR_LAST];
	XFontSet fontset;
	unsigned long fg[STYLE_LAST];
	unsigned long title[STYLE_LAST][COLOR_LAST];
	unsigned long border[STYLE_LAST][COLOR_LAST];
	unsigned long dock[COLOR_LAST];
	unsigned long notif[COLOR_LAST];
	unsigned long prompt[COLOR_LAST];
};

/* the dock */
struct Dock {
	struct Dockapp *head, *tail;    /* list of dockapps */
	Window win;                     /* dock window */
	Pixmap pix;                     /* dock pixmap */
	int x, y, w, h;                 /* dock geometry */
	int pw, ph;                     /* dock pixmap size */
	int mapped;                     /* whether dock is mapped */
};

/* window manager stuff */
struct WM {
	struct Bar *bars;                       /* list of bars */
	struct Monitor *monhead, *montail;      /* list of monitors */
	struct Notification *nhead, *ntail;     /* list of notifications */
	struct Container *c;                    /* list of containers */
	struct Container *focuslist;            /* list of containers ordered by focus history */
	struct Container *fulllist;             /* list of containers ordered from topmost to bottommost */
	struct Container *abovelist;            /* list of containers ordered from topmost to bottommost */
	struct Container *centerlist;           /* list of containers ordered from topmost to bottommost */
	struct Container *belowlist;            /* list of containers ordered from topmost to bottommost */
	struct Container *focused;              /* pointer to focused container */
	struct Container *prevfocused;          /* pointer to previously focused container */
	struct Monitor *selmon;                 /* pointer to selected monitor */
	Window wmcheckwin;                      /* dummy window required by EWMH */
	Window focuswin;                        /* dummy window to get focus when no other window has it */
	Window layerwins[LAYER_LAST];           /* dummy windows used to set stacking order */
	int nclients;                           /* total number of client windows */
	int showingdesk;                        /* whether the desktop is being shown */
	int minsize;                            /* minimum size of a container */
};

/* configuration */
struct Config {
	int ndesktops;
	int notifgap;
	int dockwidth, dockspace;
	int snap;
	int borderwidth;
	int titlewidth;
	const char *font;
	const char *notifgravity;
	const char *dockgravity;
	const char *foreground[STYLE_LAST];
	const char *titlecolors[STYLE_LAST];
	const char *bordercolors[STYLE_LAST];
	const char *dockcolors;
	const char *notifcolors;
	const char *promptcolors;

	/* the values below are computed from the values above */
	int corner;
	int divwidth;
	int minsize;
};

/* global variables */
static XSetWindowAttributes clientswa = {
	.event_mask = SubstructureNotifyMask | ExposureMask
	            | SubstructureRedirectMask | ButtonPressMask | FocusChangeMask
};
static int (*xerrorxlib)(Display *, XErrorEvent *);
static XrmDatabase xdb;
static Display *dpy;
static Window root;
static GC gc;
static Atom atoms[ATOM_LAST];
static struct Visual visual;
static struct WM wm;
static struct Dock dock;
static int depth;
static int screen, screenw, screenh;
static int cflag = 0;
static char *wmname;
static char *xrm;
volatile sig_atomic_t running = 1;

#include "config.h"

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: shod [-c] [-D dockspec] [-f buttons] [-m modifier]\n");
	(void)fprintf(stderr, "            [-N notificationspec] [-n ndesks] [-r buttons]\n");
	(void)fprintf(stderr, "            [-s snap]\n");
	exit(1);
}

/* get maximum */
static int
max(int x, int y)
{
	return x > y ? x : y;
}

/* get minimum */
static int
min(int x, int y)
{
	return x < y ? x : y;
}

/* call malloc checking for error */
static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

/* call strndup checking for error */
static char *
estrndup(const char *s, size_t maxlen)
{
	char *p;

	if ((p = strndup(s, maxlen)) == NULL)
		err(1, "strndup");
	return p;
}

/* call calloc checking for error */
static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "malloc");
	return p;
}

/* get color from color string */
static unsigned long
ealloccolor(const char *s)
{
	XColor color;

	if(!XAllocNamedColor(dpy, DefaultColormap(dpy, screen), s, &color, &color)) {
		warnx("could not allocate color: %s", s);
		return BlackPixel(dpy, screen);
	}
	return color.pixel;
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

	errx(1, "Fatal request. Request code=%d, error code=%d", e->request_code, e->error_code);
	return xerrorxlib(dpy, e);
}

/* stop running */
static void
siginthandler(int signo)
{
	(void)signo;
	running = 0;
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	long n;
	char *type;
	XrmValue xval;

	if (xrm == NULL || xdb == NULL)
		return;
	if (XrmGetResource(xdb, "shod.dockColors", "*", &type, &xval) == True)
		config.dockcolors = xval.addr;
	if (XrmGetResource(xdb, "shod.notifColors", "*", &type, &xval) == True)
		config.notifcolors = xval.addr;
	if (XrmGetResource(xdb, "shod.promptColors", "*", &type, &xval) == True)
		config.promptcolors = xval.addr;
	if (XrmGetResource(xdb, "shod.title.font", "*", &type, &xval) == True)
		config.font = xval.addr;
	if (XrmGetResource(xdb, "shod.title.foreground", "*", &type, &xval) == True) {
		config.foreground[0] = xval.addr;
		config.foreground[1] = xval.addr;
		config.foreground[2] = xval.addr;
	}
	if (XrmGetResource(xdb, "shod.title.activeColors", "*", &type, &xval) == True)
		config.titlecolors[FOCUSED] = xval.addr;
	if (XrmGetResource(xdb, "shod.title.inactiveColors", "*", &type, &xval) == True)
		config.titlecolors[UNFOCUSED] = xval.addr;
	if (XrmGetResource(xdb, "shod.title.urgentColors", "*", &type, &xval) == True)
		config.titlecolors[URGENT] = xval.addr;
	if (XrmGetResource(xdb, "shod.border.activeColors", "*", &type, &xval) == True)
		config.bordercolors[FOCUSED] = xval.addr;
	if (XrmGetResource(xdb, "shod.border.inactiveColors", "*", &type, &xval) == True)
		config.bordercolors[UNFOCUSED] = xval.addr;
	if (XrmGetResource(xdb, "shod.border.urgentColors", "*", &type, &xval) == True)
		config.bordercolors[URGENT] = xval.addr;
	if (XrmGetResource(xdb, "shod.borderWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.borderwidth = n;
	if (XrmGetResource(xdb, "shod.titleWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.titlewidth = n;
}

/* read command-line options */
static void
getoptions(int argc, char *argv[])
{
	long n;
	int c;
	char *s;

	if ((wmname = strrchr(argv[0], '/')) != NULL)
		wmname++;
	else
		wmname = argv[0];
	while ((c = getopt(argc, argv, "cD:f:m:N:n:r:s:")) != -1) {
		switch (c) {
		case 'c':
			cflag = 1;
			break;
		case 'D':
			if (*optarg == '\0' || *optarg == ':')
				break;
			config.dockgravity = optarg;
			if ((s = strchr(optarg, ':')) == NULL)
				break;
			*(s++) = '\0';
			if ((n = strtol(s, NULL, 10)) > 0)
				config.dockwidth = n;
			break;
		case 'N':
			if (*optarg == '\0' || *optarg == ':')
				break;
			config.notifgravity = optarg;
			if ((s = strchr(optarg, ':')) == NULL)
				break;
			*(s++) = '\0';
			if ((n = strtol(s, NULL, 10)) > 0)
				config.notifgap = n;
			break;
		case 'n':
			if ((n = strtol(optarg, NULL, 10)) > 0)
				config.ndesktops = n;
			break;
		case 's':
			if ((n = strtol(optarg, NULL, 10)) > 0)
				config.snap = n;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 0) {
		usage();
	}
}

/* initialize signals */
static void
initsignal(void)
{
	struct sigaction sa;

	/* remove zombies, we may inherit children when exec'ing shod in .xinitrc */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		err(1, "sigaction");

	/* set running to 0 */
	sa.sa_handler = siginthandler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1)
		err(1, "sigaction");
}

/* create dummy windows used for controlling focus and the layer of clients */
static void
initdummywindows(void)
{
	XSetWindowAttributes swa;
	int i;

	swa.do_not_propagate_mask = NoEventMask;
	wm.wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	wm.focuswin = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                            CopyFromParent, CopyFromParent, CopyFromParent,
	                            CWDontPropagate, &swa);
	for (i = 0; i < LAYER_LAST; i++) {
		wm.layerwins[i] = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
		XRaiseWindow(dpy, wm.layerwins[i]);
	}
}

/* initialize font set */
static void
initfontset(void)
{
	char **dp, *ds;
	int di;

	if ((visual.fontset = XCreateFontSet(dpy, config.font, &dp, &di, &ds)) == NULL)
		errx(1, "XCreateFontSet: could not create fontset");
	XFreeStringList(dp);
}

/* initialize cursors */
static void
initcursors(void)
{
	visual.cursors[CURSOR_NORMAL] = XCreateFontCursor(dpy, XC_left_ptr);
	visual.cursors[CURSOR_MOVE] = XCreateFontCursor(dpy, XC_fleur);
	visual.cursors[CURSOR_NW] = XCreateFontCursor(dpy, XC_top_left_corner);
	visual.cursors[CURSOR_NE] = XCreateFontCursor(dpy, XC_top_right_corner);
	visual.cursors[CURSOR_SW] = XCreateFontCursor(dpy, XC_bottom_left_corner);
	visual.cursors[CURSOR_SE] = XCreateFontCursor(dpy, XC_bottom_right_corner);
	visual.cursors[CURSOR_N] = XCreateFontCursor(dpy, XC_top_side);
	visual.cursors[CURSOR_S] = XCreateFontCursor(dpy, XC_bottom_side);
	visual.cursors[CURSOR_W] = XCreateFontCursor(dpy, XC_left_side);
	visual.cursors[CURSOR_E] = XCreateFontCursor(dpy, XC_right_side);
	visual.cursors[CURSOR_V] = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
	visual.cursors[CURSOR_H] = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
	visual.cursors[CURSOR_PIRATE] = XCreateFontCursor(dpy, XC_pirate);
}

/* initialize atom arrays */
static void
initatoms(void)
{
	char *atomnames[ATOM_LAST] = {
		[UTF8_STRING]                          = "UTF8_STRING",
		[WM_DELETE_WINDOW]                     = "WM_DELETE_WINDOW",
		[WM_WINDOW_ROLE]                       = "WM_WINDOW_ROLE",
		[WM_TAKE_FOCUS]                        = "WM_TAKE_FOCUS",
		[WM_PROTOCOLS]                         = "WM_PROTOCOLS",
		[WM_STATE]                             = "WM_STATE",
		[_NET_SUPPORTED]                       = "_NET_SUPPORTED",
		[_NET_CLIENT_LIST]                     = "_NET_CLIENT_LIST",
		[_NET_CLIENT_LIST_STACKING]            = "_NET_CLIENT_LIST_STACKING",
		[_NET_NUMBER_OF_DESKTOPS]              = "_NET_NUMBER_OF_DESKTOPS",
		[_NET_CURRENT_DESKTOP]                 = "_NET_CURRENT_DESKTOP",
		[_NET_ACTIVE_WINDOW]                   = "_NET_ACTIVE_WINDOW",
		[_NET_WM_DESKTOP]                      = "_NET_WM_DESKTOP",
		[_NET_SUPPORTING_WM_CHECK]             = "_NET_SUPPORTING_WM_CHECK",
		[_NET_SHOWING_DESKTOP]                 = "_NET_SHOWING_DESKTOP",
		[_NET_CLOSE_WINDOW]                    = "_NET_CLOSE_WINDOW",
		[_NET_MOVERESIZE_WINDOW]               = "_NET_MOVERESIZE_WINDOW",
		[_NET_WM_MOVERESIZE]                   = "_NET_WM_MOVERESIZE",
		[_NET_WM_NAME]                         = "_NET_WM_NAME",
		[_NET_WM_WINDOW_TYPE]                  = "_NET_WM_WINDOW_TYPE",
		[_NET_WM_WINDOW_TYPE_DESKTOP]          = "_NET_WM_WINDOW_TYPE_DESKTOP",
		[_NET_WM_WINDOW_TYPE_MENU]             = "_NET_WM_WINDOW_TYPE_MENU",
		[_NET_WM_WINDOW_TYPE_TOOLBAR]          = "_NET_WM_WINDOW_TYPE_TOOLBAR",
		[_NET_WM_WINDOW_TYPE_DOCK]             = "_NET_WM_WINDOW_TYPE_DOCK",
		[_NET_WM_WINDOW_TYPE_DIALOG]           = "_NET_WM_WINDOW_TYPE_DIALOG",
		[_NET_WM_WINDOW_TYPE_UTILITY]          = "_NET_WM_WINDOW_TYPE_UTILITY",
		[_NET_WM_WINDOW_TYPE_SPLASH]           = "_NET_WM_WINDOW_TYPE_SPLASH",
		[_NET_WM_WINDOW_TYPE_PROMPT]           = "_NET_WM_WINDOW_TYPE_PROMPT",
		[_NET_WM_WINDOW_TYPE_NOTIFICATION]     = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
		[_NET_WM_STATE]                        = "_NET_WM_STATE",
		[_NET_WM_STATE_STICKY]                 = "_NET_WM_STATE_STICKY",
		[_NET_WM_STATE_MAXIMIZED_VERT]         = "_NET_WM_STATE_MAXIMIZED_VERT",
		[_NET_WM_STATE_MAXIMIZED_HORZ]         = "_NET_WM_STATE_MAXIMIZED_HORZ",
		[_NET_WM_STATE_SHADED]                 = "_NET_WM_STATE_SHADED",
		[_NET_WM_STATE_HIDDEN]                 = "_NET_WM_STATE_HIDDEN",
		[_NET_WM_STATE_FULLSCREEN]             = "_NET_WM_STATE_FULLSCREEN",
		[_NET_WM_STATE_ABOVE]                  = "_NET_WM_STATE_ABOVE",
		[_NET_WM_STATE_BELOW]                  = "_NET_WM_STATE_BELOW",
		[_NET_WM_STATE_FOCUSED]                = "_NET_WM_STATE_FOCUSED",
		[_NET_WM_STATE_DEMANDS_ATTENTION]      = "_NET_WM_STATE_DEMANDS_ATTENTION",
		[_NET_WM_STRUT]                        = "_NET_WM_STRUT",
		[_NET_WM_STRUT_PARTIAL]                = "_NET_WM_STRUT_PARTIAL",
		[_NET_REQUEST_FRAME_EXTENTS]           = "_NET_REQUEST_FRAME_EXTENTS",
		[_NET_FRAME_EXTENTS]                   = "_NET_FRAME_EXTENTS",
		[_SHOD_GROUP_TAB]                      = "_SHOD_GROUP_TAB",
		[_SHOD_GROUP_CONTAINER]                = "_SHOD_GROUP_CONTAINER",
	};

	XInternAtoms(dpy, atomnames, ATOM_LAST, False, atoms);
}

/* set up root window */
static void
initroot(void)
{
	XSetWindowAttributes swa;

	/* Select SubstructureRedirect events on root window */
	swa.cursor = visual.cursors[CURSOR_NORMAL];
	swa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
	               | StructureNotifyMask | ButtonPressMask;
	XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &swa);

	/* Set focus to root window */
	XSetInputFocus(dpy, root, RevertToParent, CurrentTime);
}

/* allocate the three colors of an object */
static void
createcolors(unsigned long *colors, const char *str)
{
	int i;
	char *s, *t, *p;

	p = t = s = estrndup(str, strlen(str));
	for (i = 0; i < COLOR_LAST; i++) {
		while (*p && *p != ':')
			p++;
		*(p++) = '\0';
		if (t == 0 || *t != '\0')
			colors[i] = ealloccolor(t);
		else
			colors[i] = colors[i - 1];
		t = p;
	}
	free(s);
}

/* initialize decoration pixmap */
static void
initvisual(void)
{
	int i;

	config.corner = config.borderwidth + config.titlewidth;
	config.divwidth = config.borderwidth;
	config.minsize = config.corner * 2 + 10;
	for (i = 0; i < STYLE_LAST; i++) {
		createcolors(visual.title[i], config.titlecolors[i]);
		createcolors(visual.border[i], config.bordercolors[i]);
		visual.fg[i] = ealloccolor(config.foreground[i]);
	}
	createcolors(visual.dock, config.dockcolors);
	createcolors(visual.notif, config.notifcolors);
	createcolors(visual.prompt, config.promptcolors);
}

/* create dock window */
static void
initdock(void)
{
	XSetWindowAttributes swa;

	dock.pix = None;
	dock.win = None;
	swa.event_mask = SubstructureNotifyMask | SubstructureRedirectMask | ExposureMask;
	swa.background_pixel = visual.dock[0];
	dock.win = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
		                     CopyFromParent, CopyFromParent, CopyFromParent,
		                     CWEventMask | CWBackPixel, &swa);
}

/* get pointer to proper structure given a window */
static struct Winres
getwin(Window win)
{
	struct Winres res;
	struct Dockapp *dapp;
	struct Bar *bar;
	struct Container *c;
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	struct Notification *n;
	int i;

	res.dock = NULL;
	res.dapp = NULL;
	res.bar = NULL;
	res.row = NULL;
	res.n = NULL;
	res.c = NULL;
	res.t = NULL;
	res.d = NULL;
	if (win == dock.win) {
		res.dock = &dock;
		return res;
	}
	for (dapp = dock.head; dapp != NULL; dapp = dapp->next) {
		if (win == dapp->win) {
			res.dapp = dapp;
			return res;
		}
	}
	for (bar = wm.bars; bar != NULL; bar = bar->next) {
		if (win == bar->win) {
			res.bar = bar;
			return res;
		}
	}
	for (n = wm.nhead; n != NULL; n = n->next) {
		if (win == n->frame || win == n->win) {
			res.n = n;
			return res;
		}
	}
	for (c = wm.c; c != NULL; c = c->next) {
		if (win == c->frame) {
			res.c = c;
			return res;
		}
		for (i = 0; i < BORDER_LAST; i++) {
			if (win == c->curswin[i]) {
				res.c = c;
				return res;
			}
		}
		for (col = c->cols; col != NULL; col = col->next) {
			for (row = col->rows; row != NULL; row = row->next) {
				if (win == row->bar || win == row->bl || win == row->br) {
					res.c = c;
					res.row = row;
					return res;
				}
				for (t = row->tabs; t != NULL; t = t->next) {
					if (win == t->win || win == t->frame || win == t->title) {
						res.c = c;
						res.t = t;
						return res;
					}
					for (d = t->ds; d != NULL; d = d->next) {
						if (win == d->win || win == d->frame) {
							res.c = c;
							res.t = t;
							res.d = d;
							return res;
						}
					}
				}
			}
		}

	}
	return res;
}

/* get monitor given coordinates */
static struct Monitor *
getmon(int x, int y)
{
	struct Monitor *mon;

	for (mon = wm.monhead; mon; mon = mon->next)
		if (x >= mon->mx && x <= mon->mx + mon->mw &&
		    y >= mon->my && y <= mon->my + mon->mh)
			return mon;
	return NULL;
}

/* get window name */
char *
getwinname(Window win)
{
	XTextProperty tprop;
	char **list = NULL;
	char *name = NULL;
	unsigned char *p = NULL;
	unsigned long size, dl;
	int di;
	Atom da;

	if (XGetWindowProperty(dpy, win, atoms[_NET_WM_NAME], 0L, NAMEMAXLEN, False, atoms[UTF8_STRING],
	                       &da, &di, &size, &dl, &p) == Success && p) {
		name = estrndup((char *)p, NAMEMAXLEN);
		XFree(p);
	} else if (XGetWMName(dpy, win, &tprop) &&
		   XmbTextPropertyToTextList(dpy, &tprop, &list, &di) == Success &&
		   di > 0 && list && *list) {
		name = estrndup(*list, NAMEMAXLEN);
		XFreeStringList(list);
		XFree(tprop.value);
	}
	return name;
}

/* get atom property from window */
static Atom
getatomprop(Window win, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, win, prop, 0L, sizeof atom, False, XA_ATOM, &da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

/* get type of window both by its WMHINTS and by the _NET_WM_WINDOW_TYPE hint */
static int
getwintype(Window win)
{
	XWMHints *wmhints;
	Atom prop;
	int isdockapp;

	prop = getatomprop(win, atoms[_NET_WM_WINDOW_TYPE]);
	wmhints = XGetWMHints(dpy, win);
	isdockapp = (wmhints && (wmhints->flags & (IconWindowHint | StateHint)) && wmhints->initial_state == WithdrawnState);
	if (wmhints != NULL)
		XFree(wmhints);
	if (isdockapp) {
		return TYPE_DOCKAPP;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DESKTOP]) {
		return TYPE_DESKTOP;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_DOCK]) {
		return TYPE_DOCK;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_NOTIFICATION]) {
		return TYPE_NOTIFICATION;
	} else if (prop == atoms[_NET_WM_WINDOW_TYPE_PROMPT]) {
		return TYPE_PROMPT;
	}
	return TYPE_NORMAL;
}

/* get atom property from window */
static unsigned long
getcardprop(Window win, Atom prop, unsigned long **array)
{
	int di;
	unsigned long len;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da = None;

	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_CARDINAL, &da, &di, &len, &dl, &p) != Success || p == NULL) {
		*array = NULL;
		return 0;
	}
	*array = (unsigned long *)p;
	return len;
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

/* get tab given window is a dialog for */
static struct Tab *
getdialogfor(Window win)
{
	struct Winres res;
	Window tmpwin;

	if (XGetTransientForHint(dpy, win, &tmpwin)) {
		res = getwin(tmpwin);
		return res.t;
	}
	return NULL;
}

/* increment number of clients */
static void
clientsincr(void)
{
	wm.nclients++;
}

/* decrement number of clients */
static void
clientsdecr(void)
{
	wm.nclients--;
}

/* get focused fullscreen window in given monitor and desktop */
static struct Container *
getfullscreen(struct Monitor *mon, struct Desktop *desk)
{
	struct Container *c;

	for (c = wm.fulllist; c != NULL; c = c->rnext)
		if (!c->isminimized && c->mon == mon && (c->issticky || c->desk == desk))
			return c;
	return NULL;
}

/* get next focused container after old on given monitor and desktop */
static struct Container *
getnextfocused(struct Monitor *mon, struct Desktop *desk)
{
	struct Container *c;

	for (c = wm.focuslist; c != NULL; c = c->fnext) {
		if (c->mon == mon && (c->issticky || c->desk == desk)) {
			break;
		}
	}
	return c;
}

/* get next focused container in given direction from rel (relative) */
static struct Container *
getfocusedbydirection(struct Container *rel, int dir)
{
	struct Monitor *mon;
	struct Desktop *desk;
	struct Container *c, *ret;
	int retx, rety, relx, rely, x, y;

	ret = NULL;
	relx = rel->x + rel->w / 2;
	rely = rel->y + rel->h / 2;
	mon = rel->mon;
	desk = rel->issticky ? rel->mon->seldesk : rel->desk;
	for (c = wm.focuslist; c != NULL; c = c->fnext) {
		if (c == rel || c->isminimized)
			continue;
		if (c->mon == mon && (c->issticky || c->desk == desk)) {
			x = c->x + c->w / 2;
			y = c->y + c->h / 2;
			switch (dir) {
			case _SHOD_FOCUS_LEFT_CONTAINER:
				if (x <= relx && (ret == NULL || x > retx))
					ret = c;
				break;
			case _SHOD_FOCUS_RIGHT_CONTAINER:
				if (x >= relx && (ret == NULL || x < retx))
					ret = c;
				break;
			case _SHOD_FOCUS_TOP_CONTAINER:
				if (y <= rely && (ret == NULL || y > rety))
					ret = c;
				break;
			case _SHOD_FOCUS_BOTTOM_CONTAINER:
				if (y >= rely && (ret == NULL || y < rety))
					ret = c;
				break;
			default:
				return NULL;
			}
			if (ret != NULL) {
				retx = ret->x + ret->w / 2;
				rety = ret->y + ret->h / 2;
			}
		}
	}
	return ret;
}

/* get first focused container in the same monitor and desktop of rel */
static struct Container *
getfirstfocused(struct Container *rel)
{
	struct Monitor *mon;
	struct Desktop *desk;
	struct Container *c;

	mon = rel->mon;
	desk = rel->issticky ? rel->mon->seldesk : rel->desk;
	for (c = wm.focuslist; c != NULL; c = c->fnext) {
		if (c == rel)
			return NULL;
		if (c->isminimized)
			continue;
		if (c->mon == mon && (c->issticky || c->desk == desk)) {
			return c;
		}
	}
	return NULL;
}

/* get last focused container in the same monitor and desktop of rel */
static struct Container *
getlastfocused(struct Container *rel)
{
	struct Monitor *mon;
	struct Desktop *desk;
	struct Container *c, *ret;

	ret = NULL;
	mon = rel->mon;
	desk = rel->issticky ? rel->mon->seldesk : rel->desk;
	for (c = rel; c != NULL; c = c->fnext) {
		if (c->isminimized)
			continue;
		if (c != rel && c->mon == mon && (c->issticky || c->desk == desk)) {
			ret = c;
		}
	}
	return ret;
}

/* get pointer position within a container */
static enum Octant
getoctant(struct Container *c, int x, int y)
{
	if (c == NULL || c->isminimized)
		return 0;
	if ((y < c->b && x <= config.corner) || (x < c->b && y <= config.corner)) {
		return NW;
	} else if ((y < c->b && x >= c->w - config.corner) || (x > c->w - c->b && y <= config.corner)) {
		return NE;
	} else if ((y > c->h - c->b && x <= config.corner) || (x < c->b && y >= c->h - config.corner)) {
		return SW;
	} else if ((y > c->h - c->b && x >= c->w - config.corner) || (x > c->w - c->b && y >= c->h - config.corner)) {
		return SE;
	} else if (y < c->b) {
		return N;
	} else if (y >= c->h - c->b) {
		return S;
	} else if (x < c->b) {
		return W;
	} else if (x >= c->w - c->b) {
		return E;
	}
	return 0;
}

/* get row or column next to division the pointer is on */
static void
getdivisions(struct Container *c, struct Column **cdiv, struct Row **rdiv, int x, int y)
{
	struct Column *col;
	struct Row *row;

	*cdiv = NULL;
	*rdiv = NULL;
	for (col = c->cols; col != NULL; col = col->next) {
		if (col->next != NULL && x >= col->x + col->w && x < col->x + col->w + config.divwidth) {
			*cdiv = col;
			return;
		}
		if (x >= col->x && x < col->x + col->w) {
			for (row = col->rows; row != NULL; row = row->next) {
				if (row->next != NULL && y >= row->y + row->h && y < row->y + row->h + config.divwidth) {
					*rdiv = row;
					return;
				}
			}
		}
	}
}

/* get pointer to nth desktop in focused monitor */
static struct Desktop *
getdesk(long int n, long int m)
{
	struct Monitor *mon, *tmp;
	long int i;

	if (n < 0 || n >= config.ndesktops)
		return NULL;
	if (m == 0) {
		return &wm.selmon->desks[n];
	} else {
		mon = NULL;
		for (i = 0, tmp = wm.selmon; i < m && tmp != NULL; i++, tmp = tmp->next)
			mon = tmp;
		if (mon != NULL) {
		return &mon->desks[n];
		}
	}
	return NULL;
}

/* check whether window was placed by the user */
static int
isuserplaced(Window win)
{
	XSizeHints size;
	long dl;

	return (XGetWMNormalHints(dpy, win, &size, &dl) && (size.flags & USPosition));
}

/* set icccm wmstate */
static void
icccmwmstate(Window win, int state)
{
	long data[2];

	data[0] = state;
	data[1] = None;
	XChangeProperty(dpy, win, atoms[WM_STATE], atoms[WM_STATE], 32, PropModeReplace, (unsigned char *)&data, 2);
}

/* delete window state property */
static void
icccmdeletestate(Window win)
{
	XDeleteProperty(dpy, win, atoms[WM_STATE]);
}

/* initialize ewmh hints */
static void
ewmhinit(void)
{
	/* set window and property that indicates that the wm is ewmh compliant */
	XChangeProperty(dpy, wm.wmcheckwin, atoms[_NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wm.wmcheckwin, 1);
	XChangeProperty(dpy, wm.wmcheckwin, atoms[_NET_WM_NAME], atoms[UTF8_STRING], 8, PropModeReplace, (unsigned char *)wmname, strlen(wmname));
	XChangeProperty(dpy, root, atoms[_NET_SUPPORTING_WM_CHECK], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&wm.wmcheckwin, 1);

	/* set properties that the window manager supports */
	XChangeProperty(dpy, root, atoms[_NET_SUPPORTED], XA_ATOM, 32, PropModeReplace, (unsigned char *)atoms, ATOM_LAST);
	XDeleteProperty(dpy, root, atoms[_NET_CLIENT_LIST]);

	/* set number of desktops */
	XChangeProperty(dpy, root, atoms[_NET_NUMBER_OF_DESKTOPS], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&config.ndesktops, 1);
}

/* set current desktop hint */
static void
ewmhsetcurrentdesktop(unsigned long n)
{
	XChangeProperty(dpy, root, atoms[_NET_CURRENT_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
}

/* set showing desktop hint */
static void
ewmhsetshowingdesktop(int n)
{
	XChangeProperty(dpy, root, atoms[_NET_SHOWING_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&n, 1);
}

/* set list of clients hint */
static void
ewmhsetclients(void)
{
	struct Container *c;
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	Window *wins = NULL;
	size_t i = 0;

	if (wm.nclients < 1)
		return;
	wins = ecalloc(wm.nclients, sizeof *wins);
	for (c = wm.c; c != NULL; c = c->next) {
		for (col = c->cols; col != NULL; col = col->next) {
			for (row = col->rows; row != NULL; row = row->next) {
				for (t = row->tabs; t != NULL; t = t->next) {
					wins[i++] = t->win;
					for (d = t->ds; d != NULL; d = d->next) {
						wins[i++] = d->win;
					}
				}
			}
		}
	}
	XChangeProperty(dpy, root, atoms[_NET_CLIENT_LIST], XA_WINDOW, 32, PropModeReplace, (unsigned char *)wins, i);
	free(wins);
}

/* set stacking list of clients hint */
static void
ewmhsetclientsstacking(void)
{
	struct Container *c;
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	Window *wins = NULL;
	size_t i = 0;

	if (wm.nclients < 1)
		return;
	wins = ecalloc(wm.nclients, sizeof *wins);
	i = wm.nclients;
	for (c = wm.fulllist; c != NULL; c = c->rnext) {
		for (col = c->cols; col != NULL; col = col->next) {
			for (row = col->rows; row != NULL; row = row->next) {
				for (t = row->tabs; t != NULL; t = t->next) {
					wins[--i] = t->win;
					for (d = t->ds; d != NULL; d = d->next) {
						wins[--i] = d->win;
					}
				}
			}
		}
	}
	for (c = wm.abovelist; c != NULL; c = c->rnext) {
		for (col = c->cols; col != NULL; col = col->next) {
			for (row = col->rows; row != NULL; row = row->next) {
				for (t = row->tabs; t != NULL; t = t->next) {
					wins[--i] = t->win;
					for (d = t->ds; d != NULL; d = d->next) {
						wins[--i] = d->win;
					}
				}
			}
		}
	}
	for (c = wm.centerlist; c != NULL; c = c->rnext) {
		for (col = c->cols; col != NULL; col = col->next) {
			for (row = col->rows; row != NULL; row = row->next) {
				for (t = row->tabs; t != NULL; t = t->next) {
					wins[--i] = t->win;
					for (d = t->ds; d != NULL; d = d->next) {
						wins[--i] = d->win;
					}
				}
			}
		}
	}
	for (c = wm.belowlist; c != NULL; c = c->rnext) {
		for (col = c->cols; col != NULL; col = col->next) {
			for (row = col->rows; row != NULL; row = row->next) {
				for (t = row->tabs; t != NULL; t = t->next) {
					wins[--i] = t->win;
					for (d = t->ds; d != NULL; d = d->next) {
						wins[--i] = d->win;
					}
				}
			}
		}
	}
	XChangeProperty(dpy, root, atoms[_NET_CLIENT_LIST_STACKING], XA_WINDOW, 32, PropModeReplace, (unsigned char *)wins+i, wm.nclients-i);
	free(wins);
}

/* set active window hint */
static void
ewmhsetactivewindow(Window w)
{
	XChangeProperty(dpy, root, atoms[_NET_ACTIVE_WINDOW], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&w, 1);
}

/* set desktop for a given window */
static void
ewmhsetdesktop(Window win, long d)
{
	XChangeProperty(dpy, win, atoms[_NET_WM_DESKTOP], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&d, 1);
}

/* set desktop for all windows in a container */
static void
ewmhsetwmdesktop(struct Container *c)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	long n;

	n = (c->issticky || c->isminimized) ? 0xFFFFFFFF : c->desk->n;
	for (col = c->cols; col != NULL; col = col->next) {
		for (row = col->rows; row != NULL; row = row->next) {
			for (t = row->tabs; t != NULL; t = t->next) {
				ewmhsetdesktop(t->win, n);
				for (d = t->ds; d; d = d->next) {
					ewmhsetdesktop(d->win, n);
				}
			}
		}
	}
}

/* set frames of window */
static void
ewmhsetframeextents(Window win, int b, int t)
{
	unsigned long data[4];

	data[0] = data[1] = data[3] = b;
	data[2] = b + t;
	XChangeProperty(dpy, win, atoms[_NET_FRAME_EXTENTS], XA_CARDINAL, 32, PropModeReplace, (unsigned char *)data, 4);
}

/* set state of windows */
static void
ewmhsetstate(struct Container *c)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	Atom data[9];
	int n = 0;

	if (c == NULL)
		return;
	if (c == wm.focused)
		data[n++] = atoms[_NET_WM_STATE_FOCUSED];
	if (c->isfullscreen)
		data[n++] = atoms[_NET_WM_STATE_FULLSCREEN];
	if (c->issticky)
		data[n++] = atoms[_NET_WM_STATE_STICKY];
	if (c->isshaded)
		data[n++] = atoms[_NET_WM_STATE_SHADED];
	if (c->isminimized)
		data[n++] = atoms[_NET_WM_STATE_HIDDEN];
	if (c->ismaximized) {
		data[n++] = atoms[_NET_WM_STATE_MAXIMIZED_VERT];
		data[n++] = atoms[_NET_WM_STATE_MAXIMIZED_HORZ];
	}
	if (c->layer > 0)
		data[n++] = atoms[_NET_WM_STATE_ABOVE];
	else if (c->layer < 0)
		data[n++] = atoms[_NET_WM_STATE_BELOW];
	for (col = c->cols; col != NULL; col = col->next) {
		for (row = col->rows; row != NULL; row = row->next) {
			for (t = row->tabs; t != NULL; t = t->next) {
				XChangeProperty(dpy, t->win, atoms[_NET_WM_STATE], XA_ATOM, 32, PropModeReplace, (unsigned char *)data, n);
				for (d = t->ds; d != NULL; d = d->next) {
					XChangeProperty(dpy, d->win, atoms[_NET_WM_STATE], XA_ATOM, 32, PropModeReplace, (unsigned char *)data, n);
				}
			}
		}
	}
}

/* set group of windows in client */
static void
shodgrouptab(struct Container *c)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;

	for (col = c->cols; col != NULL; col = col->next) {
		for (row = col->rows; row != NULL; row = row->next) {
			for (t = row->tabs; t != NULL; t = t->next) {
				XChangeProperty(dpy, t->win, atoms[_SHOD_GROUP_TAB], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&row->seltab->win, 1);
			}
		}
	}
}

/* set group of windows in client */
static void
shodgroupcontainer(struct Container *c)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;

	for (col = c->cols; col != NULL; col = col->next) {
		for (row = col->rows; row != NULL; row = row->next) {
			for (t = row->tabs; t != NULL; t = t->next) {
				XChangeProperty(dpy, t->win, atoms[_SHOD_GROUP_CONTAINER], XA_WINDOW, 32, PropModeReplace, (unsigned char *)&c->selcol->selrow->seltab->win, 1);
			}
		}
	}
}

/* send a WM_DELETE message to client */
static void
winclose(Window win)
{
	XEvent ev;

	ev.type = ClientMessage;
	ev.xclient.window = win;
	ev.xclient.message_type = atoms[WM_PROTOCOLS];
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = atoms[WM_DELETE_WINDOW];
	ev.xclient.data.l[1] = CurrentTime;

	/*
	 * communicate with the given Client, kindly telling it to
	 * close itself and terminate any associated processes using
	 * the WM_DELETE_WINDOW protocol
	 */
	XSendEvent(dpy, win, False, NoEventMask, &ev);
}

/* notify window of configuration changing */
static void
winnotify(Window win, int x, int y, int w, int h)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.x = x;
	ce.y = y;
	ce.width = w;
	ce.height = h;
	ce.border_width = 0;
	ce.above = None;
	ce.override_redirect = False;
	ce.event = win;
	ce.window = win;
	XSendEvent(dpy, win, False, StructureNotifyMask, (XEvent *)&ce);
}

/* check whether window is urgent */
static int
winisurgent(Window win)
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

/* get tab decoration style */
static int
tabgetstyle(struct Tab *t)
{
	if (t->isurgent)
		return URGENT;
	if (t->row->col->c == wm.focused)
		return FOCUSED;
	return UNFOCUSED;
}

/* get decoration style (and state) of container */
static int
containergetstyle(struct Container *c)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;

	if (c == wm.focused)
		return FOCUSED;
	for (col = c->cols; col != NULL; col = col->next)
		for (row = col->rows; row != NULL; row = row->next)
			for (t = row->tabs; t != NULL; t = t->next)
				if (t->isurgent)
					return URGENT;
	return UNFOCUSED;
}

/* check if container can be shaded */
static int
containerisshaded(struct Container *c)
{
	return c->isshaded && !c->isfullscreen;
}

/* calculate size of dialogs of a tab */
static void
dialogcalcsize(struct Dialog *d)
{
	struct Tab *t;

	t = d->t;
	d->w = max(1, min(d->maxw, t->winw - 2 * config.borderwidth));
	d->h = max(1, min(d->maxh, t->winh - 2 * config.borderwidth));
	d->x = t->winw / 2 - d->w / 2;
	d->y = t->winh / 2 - d->h / 2;
}

/* calculate position and width of tabs of a row */
static void
rowcalctabs(struct Row *row)
{
	struct Dialog *d;
	struct Tab *t;
	int i, x;

	x = config.titlewidth;
	for (i = 0, t = row->tabs; t != NULL; t = t->next, i++) {
		if (row == row->col->maxrow)
			t->winh = max(1, row->col->c->h - 2 * row->col->c->b - row->col->nrows * config.titlewidth);
		else
			t->winh = max(1, row->h - config.titlewidth);
		t->winw = row->col->w;
		t->w = max(1, ((i + 1) * (t->winw - 2 * config.titlewidth) / row->ntabs) - (i * (t->winw - 2 * config.titlewidth) / row->ntabs));
		t->x = x;
		x += t->w;
		for (d = t->ds; d != NULL; d = d->next) {
			dialogcalcsize(d);
		}
	}
}

/* calculate position and height of rows of a column */
static void
colcalcrows(struct Column *col, int recalcfact, int recursive)
{
	struct Container *c;
	struct Row *row;
	int i, y, h, sumh;
	int content;
	int recalc;

	c = col->c;

	/* check if rows sum up the height of the container */
	content = c->h - (col->nrows - 1) * config.divwidth - 2 * c->b;
	sumh = 0;
	recalc = 0;
	for (row = col->rows; row != NULL; row = row->next) {
		if (!recalcfact) {
			if (row->next == NULL) {
				row->h = content - sumh;
			} else {
				row->h = row->fact * content;
			}
			if (row->h <= config.titlewidth) {
				recalc = 1;
			}
		}
		sumh += row->h;
	}
	if (sumh != content)
		recalc = 1;

	if (col->c->isfullscreen && col->c->ncols == 1 && col->nrows == 1) {
		h = col->c->h + config.titlewidth;
		y = -config.titlewidth;
		recalc = 1;
	} else {
		h = col->c->h - 2 * c->b - (col->nrows - 1) * config.divwidth;
		y = c->b;
	}
	for (i = 0, row = col->rows; row != NULL; row = row->next, i++) {
		if (recalc)
			row->h = max(1, ((i + 1) * h / col->nrows) - (i * h / col->nrows));
		if (recalc || recalcfact)
			row->fact = (double)row->h/(double)c->h;
		row->y = y;
		y += row->h + config.divwidth;
		if (recursive) {
			rowcalctabs(row);
		}
	}
}

/* calculate position and width of columns of a container */
static void
containercalccols(struct Container *c, int recalcfact, int recursive)
{
	struct Column *col;
	int i, x, w;
	int sumw;
	int content;
	int recalc;

	if (c->isfullscreen) {
		c->x = c->mon->mx;
		c->y = c->mon->my;
		c->w = c->mon->mw;
		c->h = c->mon->mh;
		c->b = 0;
	} else if (c->ismaximized) {
		c->x = c->mon->wx;
		c->y = c->mon->wy;
		c->w = c->mon->ww;
		c->h = c->mon->wh;
		c->b = config.borderwidth;
	} else {
		c->x = c->nx;
		c->y = c->ny;
		c->w = c->nw;
		c->h = c->nh;
		c->b = config.borderwidth;
	}
	if (containerisshaded(c)) {
		c->h = 0;
	}

	/* check if columns sum up the width of the container */
	content = c->w - (c->ncols - 1) * config.divwidth - 2 * c->b;
	sumw = 0;
	recalc = 0;
	for (col = c->cols; col != NULL; col = col->next) {
		if (!recalcfact) {
			if (col->next == NULL) {
				col->w = content - sumw;
			} else {
				col->w = col->fact * content;
			}
			if (col->w == 0) {
				recalc = 1;
			}
		}
		sumw += col->w;
	}
	if (sumw != content)
		recalc = 1;

	w = c->w - 2 * c->b - (c->ncols - 1) * config.divwidth;
	x = c->b;
	for (i = 0, col = c->cols; col != NULL; col = col->next, i++) {
		if (containerisshaded(c)) {
			c->h = max(c->h, col->nrows * config.titlewidth);
		}
		if (recalc)
			col->w = max(1, ((i + 1) * w / c->ncols) - (i * w / c->ncols));
		if (recalc || recalcfact)
			col->fact = (double)col->w/(double)c->w;
		col->x = x;
		x += col->w + config.divwidth;
		if (recursive) {
			colcalcrows(col, recalcfact, 1);
		}
	}
	if (containerisshaded(c)) {
		c->h += 2 * c->b;
	}
}

/* find best position to place a container on screen */
static void
containerplace(struct Container *c, struct Desktop *desk, int userplaced)
{
	struct Monitor *mon;
	struct Container *tmp;
	int grid[DIV][DIV] = {{0}, {0}};
	int lowest;
	int i, j, k, w, h;
	int ha, hb, wa, wb;
	int ya, yb, xa, xb;
	int subx, suby;         /* position of the larger subregion */
	int subw, subh;         /* larger subregion width and height */
	int origw, origh;

	if (desk == NULL || c == NULL || c->isminimized)
		return;

	mon = desk->mon;

	/* if window is bigger than monitor, resize it while maintaining proportion */
	origw = c->nw;
	origh = c->nh;
	w = min(origw, mon->ww);
	h = min(origh, mon->wh);
	if (origw * h > origh * w) {
		h = (origh * w) / origw;
		w = (origw * h) / origh;
	} else {
		w = (origw * h) / origh;
		h = (origh * w) / origw;
	}
	c->nw = max(wm.minsize, w);
	c->nh = max(wm.minsize, h);

	/* if the user placed the window, we should not re-place it */
	if (userplaced)
		return;

	/*
	 * The container area is the region of the screen where containers live,
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
	for (tmp = wm.c; tmp; tmp = tmp->next) {
		if (tmp != c && !tmp->isminimized && ((tmp->issticky && tmp->mon == mon) || tmp->desk == desk)) {
			for (i = 0; i < DIV; i++) {
				for (j = 0; j < DIV; j++) {
					ha = mon->wy + (mon->wh * i)/DIV;
					hb = mon->wy + (mon->wh * (i + 1))/DIV;
					wa = mon->wx + (mon->ww * j)/DIV;
					wb = mon->wx + (mon->ww * (j + 1))/DIV;
					ya = tmp->ny;
					yb = tmp->ny + tmp->nh;
					xa = tmp->nx;
					xb = tmp->nx + tmp->nw;
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
				if (k < w)
					break;
			}
			if (k < w)
				h--;
			if (w * h > subw * subh) {
				subw = w;
				subh = h;
				suby = i;
				subx = j;
			}
		}
	}
	subx = subx * mon->ww / DIV;
	suby = suby * mon->wh / DIV;
	subw = subw * mon->ww / DIV;
	subh = subh * mon->wh / DIV;
	c->nx = min(mon->wx + mon->ww - c->nw, max(mon->wx, mon->wx + subx + subw / 2 - c->nw / 2));
	c->ny = min(mon->wy + mon->wh - c->nh, max(mon->wy, mon->wy + suby + subh / 2 - c->nh / 2));
	containercalccols(c, 0, 1);
}

/* draw rectangle shadows */
static void
drawrectangle(Pixmap pix, int doubleshadow, int x, int y, int w, int h, unsigned long top, unsigned long bot)
{
	XGCValues val;
	XRectangle recs[4];

	if (w <= 0 || h <= 0)
		return;

	/* draw light shadow */
	recs[0] = (XRectangle){.x = x + 0, .y = y + 0, .width = 1, .height = h - 1};
	recs[1] = (XRectangle){.x = x + 0, .y = y + 0, .width = w - 1, .height = 1};
	recs[2] = (XRectangle){.x = x + 1, .y = y + 1, .width = 1, .height = h - 3};
	recs[3] = (XRectangle){.x = x + 1, .y = y + 1, .width = w - 3, .height = 1};
	val.foreground = top;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, doubleshadow ? 4 : 2);

	/* draw dark shadow */
	recs[0] = (XRectangle){.x = x + w - 1, .y = y + 0, .width = 1, .height = h};
	recs[1] = (XRectangle){.x = x + 0, .y = y + h - 1, .width = w, .height = 1};
	recs[2] = (XRectangle){.x = x + w - 2, .y = y + 1, .width = 1, .height = h - 2};
	recs[3] = (XRectangle){.x = x + 1, .y = y + h - 2, .width = w - 2, .height = 1};
	val.foreground = bot;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, doubleshadow ? 4 : 2);
}

/* draw borders with shadows */
static void
drawborders(Pixmap pix, int doubleshadow, int w, int h, unsigned long *decor)
{
	XGCValues val;
	XRectangle recs[8];
	int partw, parth;

	if (w <= 0 || h <= 0)
		return;

	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;

	/* draw background */
	val.foreground = decor[COLOR_MID];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, pix, gc, 0, 0, w, h);

	/* draw light shadow */
	recs[0] = (XRectangle){.x = 0, .y = 0, .width = 1, .height = h - 1};
	recs[1] = (XRectangle){.x = 0, .y = 0, .width = w - 1, .height = 1};
	recs[2] = (XRectangle){
		.x = w - config.borderwidth,
		.y = config.borderwidth - 1,
		.width = 1,
		.height = parth + 2
	};
	recs[3] = (XRectangle){
		.x = config.borderwidth - 1,
		.y = h - config.borderwidth,
		.width = partw + 2,
		.height = 1
	};
	recs[4] = (XRectangle){.x = 1, .y = 1, .width = 1, .height = h - 2};
	recs[5] = (XRectangle){.x = 1, .y = 1, .width = w - 2, .height = 1};
	recs[6] = (XRectangle){
		.x = w - config.borderwidth + 1,
		.y = config.borderwidth - 2,
		.width = 1,
		.height = parth + 4
	};
	recs[7] = (XRectangle){
		.x = config.borderwidth - 2,
		.y = h - config.borderwidth + 1,
		.width = partw + 4,
		.height = 1
	};
	val.foreground = decor[COLOR_LIGHT];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, doubleshadow ? 8 : 4);

	/* draw dark shadow */
	recs[0] = (XRectangle){.x = w - 1, .y = 0, .width = 1, .height = h};
	recs[1] = (XRectangle){.x = 0, .y = h - 1, .width = w, .height = 1};
	recs[2] = (XRectangle){
		.x = config.borderwidth - 1,
		.y = config.borderwidth - 1,
		.width = 1,
		.height = parth + 1
	};
	recs[3] = (XRectangle){
		.x = config.borderwidth - 1,
		.y = config.borderwidth - 1,
		.width = partw + 1,
		.height = 1
	};
	recs[4] = (XRectangle){.x = w - 2, .y = 1, .width = 1, .height = h - 2};
	recs[5] = (XRectangle){.x = 1, .y = h - 2, .width = w - 2, .height = 1};
	recs[6] = (XRectangle){
		.x = config.borderwidth - 2,
		.y = config.borderwidth - 2,
		.width = 1,
		.height = parth + 3
	};
	recs[7] = (XRectangle){
		.x = config.borderwidth - 2,
		.y = config.borderwidth - 2,
		.width = partw + 3,
		.height = 1
	};
	val.foreground = decor[COLOR_DARK];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, pix, gc, recs, doubleshadow ? 8 : 4);
}

/* decorate dialog window */
static void
dialogdecorate(struct Dialog *d)
{
	unsigned long *decor;
	int fullw, fullh;       /* size of dialog window + borders */

	decor = visual.border[tabgetstyle(d->t)];
	fullw = d->w + 2 * config.borderwidth;
	fullh = d->h + 2 * config.borderwidth;

	/* (re)create pixmap */
	if (d->pw != fullw || d->ph != fullh || d->pix == None) {
		if (d->pix != None)
			XFreePixmap(dpy, d->pix);
		d->pix = XCreatePixmap(dpy, d->frame, fullw, fullh, depth);
	}
	d->pw = fullw;
	d->ph = fullh;

	drawborders(d->pix, (config.borderwidth - 4 > 0), fullw, fullh, decor);

	XCopyArea(dpy, d->pix, d->frame, gc, 0, 0, fullw, fullh, 0, 0);
}

/* decorate tab */
static void
tabdecorate(struct Tab *t, int pressed)
{
	XGCValues val;
	XRectangle box, dr;
	unsigned long mid, top, bot;
	size_t len;
	int style;
	int drawlines = 0;
	int x, y, i;

	style = tabgetstyle(t);
	mid = visual.title[style][COLOR_MID];
	if (t->row != NULL && t != t->row->col->c->selcol->selrow->seltab) {
		top = visual.title[style][COLOR_LIGHT];
		bot = visual.title[style][COLOR_DARK];
		drawlines = 0;
	} else if (t->row != NULL && pressed) {
		top = visual.title[style][COLOR_DARK];
		bot = visual.title[style][COLOR_LIGHT];
		drawlines = 1;
	} else {
		top = visual.title[style][COLOR_LIGHT];
		bot = visual.title[style][COLOR_DARK];
		drawlines = 1;
	}

	/* (re)create pixmap */
	if (t->ptw != t->w || t->pixtitle == None) {
		if (t->pixtitle != None)
			XFreePixmap(dpy, t->pixtitle);
		t->pixtitle = XCreatePixmap(dpy, t->title, t->w, config.titlewidth, depth);
	}
	t->ptw = t->w;

	if (t->pw != t->winw || t->ph != t->winh || t->pix == None) {
		if (t->pix != None)
			XFreePixmap(dpy, t->pix);
		t->pix = XCreatePixmap(dpy, t->frame, t->winw, t->winh, depth);
	}
	t->pw = t->winw;
	t->ph = t->winh;

	/* draw background */
	val.foreground = mid;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, t->pixtitle, gc, 0, 0, t->w, config.titlewidth);

	/* write tab title */
	if (t->name != NULL) {
		len = strlen(t->name);
		XmbTextExtents(visual.fontset, t->name, len, &dr, &box);
		x = (t->w - box.width) / 2 - box.x;
		y = (config.titlewidth - box.height) / 2 - box.y;

		for (i = 3; drawlines && i < config.titlewidth - 3; i += 3) {
			val.foreground = top;
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangle(dpy, t->pixtitle, gc, 4, i, x - 8, 1);
			XFillRectangle(dpy, t->pixtitle, gc, t->w - x + 2, i, x - 6, 1);
		}

		for (i = 4; drawlines && i < config.titlewidth - 2; i += 3) {
			val.foreground = bot;
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangle(dpy, t->pixtitle, gc, 4, i, x - 8, 1);
			XFillRectangle(dpy, t->pixtitle, gc, t->w - x + 2, i, x - 6, 1);
		}

		val.foreground = visual.fg[style];
		XChangeGC(dpy, gc, GCForeground, &val);
		XmbDrawString(dpy, t->pixtitle, visual.fontset, gc, x, y, t->name, len);
	}

	drawrectangle(t->pixtitle, (config.borderwidth - 4 > 0), 0, 0, t->w, config.titlewidth, top, bot);

	/* draw frame background */
	if (!pressed) {
		val.foreground = mid;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangle(dpy, t->pix, gc, 0, 0, t->winw, t->winh);
	}

	XCopyArea(dpy, t->pixtitle, t->title, gc, 0, 0, t->w, config.titlewidth, 0, 0);
	XCopyArea(dpy, t->pix, t->frame, gc, 0, 0, t->winw, t->winh, 0, 0);
}

/* draw title bar buttons */
static void
buttonleftdecorate(struct Row *row, int pressed)
{
	XGCValues val;
	XRectangle recs[2];
	unsigned long mid, top, bot;
	int style;
	int x, y, w;

	w = config.titlewidth - 9;
	style = (row->seltab) ? tabgetstyle(row->seltab) : UNFOCUSED;
	mid = visual.title[style][COLOR_MID];
	if (pressed) {
		top = visual.title[style][COLOR_DARK];
		bot = visual.title[style][COLOR_LIGHT];
	} else {
		top = visual.title[style][COLOR_LIGHT];
		bot = visual.title[style][COLOR_DARK];
	}

	/* draw background */
	val.foreground = mid;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, row->pixbl, gc, 0, 0, config.titlewidth, config.titlewidth);

	drawrectangle(row->pixbl, (config.borderwidth - 4 > 0), 0, 0, config.titlewidth, config.titlewidth, top, bot);

	if (w > 0) {
		x = 4;
		y = config.titlewidth / 2 - 1;
		recs[0] = (XRectangle){.x = x, .y = y, .width = w, .height = 1};
		recs[1] = (XRectangle){.x = x, .y = y, .width = 1, .height = 3};
		val.foreground = (pressed) ? bot : top;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, row->pixbl, gc, recs, 2);
		recs[0] = (XRectangle){.x = x + 1, .y = y + 2, .width = w, .height = 1};
		recs[1] = (XRectangle){.x = x + w, .y = y, .width = 1, .height = 3};
		val.foreground = (pressed) ? top : bot;
		XChangeGC(dpy, gc, GCForeground, &val);
		XFillRectangles(dpy, row->pixbl, gc, recs, 2);
	}

	XCopyArea(dpy, row->pixbl, row->bl, gc, 0, 0, config.titlewidth, config.titlewidth, 0, 0);
}

/* draw title bar buttons */
static void
buttonrightdecorate(struct Row *row, int pressed)
{
	XGCValues val;
	XPoint pts[9];
	unsigned long mid, top, bot;
	int style;
	int w;

	w = (config.titlewidth - 11) / 2;
	style = (row->seltab) ? tabgetstyle(row->seltab) : UNFOCUSED;
	mid = visual.title[style][COLOR_MID];
	if (pressed) {
		top = visual.title[style][COLOR_DARK];
		bot = visual.title[style][COLOR_LIGHT];
	} else {
		top = visual.title[style][COLOR_LIGHT];
		bot = visual.title[style][COLOR_DARK];
	}

	/* draw background */
	val.foreground = mid;
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, row->pixbr, gc, 0, 0, config.titlewidth, config.titlewidth);

	drawrectangle(row->pixbr, (config.borderwidth - 4 > 0), 0, 0, config.titlewidth, config.titlewidth, top, bot);

	if (w > 0) {
		pts[0] = (XPoint){.x = 3, .y = config.titlewidth - 5};
		pts[1] = (XPoint){.x = 0, .y = - 1};
		pts[2] = (XPoint){.x = w, .y = -w};
		pts[3] = (XPoint){.x = -w, .y = -w};
		pts[4] = (XPoint){.x = 0, .y = -2};
		pts[5] = (XPoint){.x = 2, .y = 0};
		pts[6] = (XPoint){.x = w, .y = w};
		pts[7] = (XPoint){.x = w, .y = -w};
		pts[8] = (XPoint){.x = 1, .y = 0};
		val.foreground = (pressed) ? bot : top;
		XChangeGC(dpy, gc, GCForeground, &val);
		XDrawLines(dpy, row->pixbr, gc, pts, 9, CoordModePrevious);

		pts[0] = (XPoint){.x = 3, .y = config.titlewidth - 4};
		pts[1] = (XPoint){.x = 2, .y = 0};
		pts[2] = (XPoint){.x = w, .y = -w};
		pts[3] = (XPoint){.x = w, .y = w};
		pts[4] = (XPoint){.x = 2, .y = 0};
		pts[5] = (XPoint){.x = 0, .y = -2};
		pts[6] = (XPoint){.x = -w, .y = -w};
		pts[7] = (XPoint){.x = w, .y = -w};
		pts[8] = (XPoint){.x = 0, .y = -2};
		val.foreground = (pressed) ? top : bot;
		XChangeGC(dpy, gc, GCForeground, &val);
		XDrawLines(dpy, row->pixbr, gc, pts, 9, CoordModePrevious);
	}

	XCopyArea(dpy, row->pixbr, row->br, gc, 0, 0, config.titlewidth, config.titlewidth, 0, 0);
}

/* draw decoration on container frame */
static void
containerdecorate(struct Container *c, struct Column *cdiv, struct Row *rdiv, int recursive, enum Octant o)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	XRectangle recs[10];
	XGCValues val;
	unsigned long *decor;
	int doubleshadow;
	int x, y, w, h;
	int isshaded;

	if (c == NULL)
		return;
	doubleshadow = config.borderwidth - 4 > 0;
	decor = visual.border[containergetstyle(c)];
	w = c->w - config.corner * 2;
	h = c->h - config.corner * 2;
	isshaded = containerisshaded(c);

	/* (re)create pixmap */
	if (c->pw != c->w || c->ph != c->h || c->pix == None) {
		if (c->pix != None)
			XFreePixmap(dpy, c->pix);
		c->pix = XCreatePixmap(dpy, c->frame, c->w, c->h, depth);
	}
	c->pw = c->w;
	c->ph = c->h;

	/* draw background */
	val.foreground = decor[COLOR_MID];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, c->pix, gc, 0, 0, c->w, c->h);

	if (c->b > 0) {
		/* top edge */
		drawrectangle(c->pix, doubleshadow, config.corner, 0, w, config.borderwidth,
		              (o == N ? decor[COLOR_DARK] : decor[COLOR_LIGHT]),
		              (o == N ? decor[COLOR_LIGHT] : decor[COLOR_DARK]));

		/* bottom edge */
		drawrectangle(c->pix, doubleshadow, config.corner, c->h - config.borderwidth, w, config.borderwidth,
		              (o == S ? decor[COLOR_DARK] : decor[COLOR_LIGHT]),
		              (o == S ? decor[COLOR_LIGHT] : decor[COLOR_DARK]));

		/* left edge */
		drawrectangle(c->pix, doubleshadow, 0, config.corner, config.borderwidth, h,
		              (o == W ? decor[COLOR_DARK] : decor[COLOR_LIGHT]),
		              (o == W ? decor[COLOR_LIGHT] : decor[COLOR_DARK]));

		/* left edge */
		drawrectangle(c->pix, doubleshadow, c->w - config.borderwidth, config.corner, config.borderwidth, h,
		              (o == E ? decor[COLOR_DARK] : decor[COLOR_LIGHT]),
		              (o == E ? decor[COLOR_LIGHT] : decor[COLOR_DARK]));

		if (isshaded) {
			/* left corner */
			x = 0;
			recs[0] = (XRectangle){.x = x + 0, .y = 0, .width = 1, .height = c->h - 1};
			recs[1] = (XRectangle){.x = x + 0, .y = 0, .width = config.corner - 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.borderwidth - 1, .y = c->h - config.borderwidth, .width = config.titlewidth, .height = 1};
			recs[3] = (XRectangle){.x = x + 1, .y = 0, .width = 1, .height = c->h - 2};
			recs[4] = (XRectangle){.x = x + 0, .y = 1, .width = config.corner - 2, .height = 1};
			recs[5] = (XRectangle){.x = x + config.borderwidth - 2, .y = c->h - config.borderwidth + 1, .width = config.titlewidth, .height = 1};
			val.foreground = (o & W) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 6 : 3);
			recs[0] = (XRectangle){.x = x + config.borderwidth - 1, .y = config.borderwidth - 1, .width = 1, .height = c->h - config.borderwidth * 2 + 1};
			recs[1] = (XRectangle){.x = x + config.borderwidth - 1, .y = config.borderwidth - 1, .width = config.titlewidth + 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.corner - 1, .y = 0, .width = 1, .height = config.borderwidth};
			recs[3] = (XRectangle){.x = x + config.corner - 1, .y = c->h - config.borderwidth, .width = 1, .height = config.borderwidth};
			recs[4] = (XRectangle){.x = x + 0, .y = c->h - 1, .width = config.corner, .height = 1};
			recs[5] = (XRectangle){.x = x + config.borderwidth - 2, .y = config.borderwidth - 2, .width = 1, .height = c->h - config.borderwidth * 2 + 3};
			recs[6] = (XRectangle){.x = x + config.borderwidth - 2, .y = config.borderwidth - 2, .width = config.titlewidth + 2, .height = 1};
			recs[7] = (XRectangle){.x = x + config.corner - 2, .y = 1, .width = 1, .height = config.borderwidth - 1};
			recs[8] = (XRectangle){.x = x + config.corner - 2, .y = c->h - config.borderwidth + 1, .width = 1, .height = config.borderwidth - 1};
			recs[9] = (XRectangle){.x = x + 1, .y = c->h - 2, .width = config.corner - 1, .height = 1};
			val.foreground = (o & W) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 10 : 5);

			/* right corner */
			x = c->w - config.corner;
			recs[0] = (XRectangle){.x = x + 0, .y = 0, .width = 1, .height = config.borderwidth - 1};
			recs[1] = (XRectangle){.x = x + 0, .y = 0, .width = config.corner - 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.titlewidth, .y = config.borderwidth - 1, .width = 1, .height = c->h - config.borderwidth * 2 + 3};
			recs[3] = (XRectangle){.x = x + 0, .y = c->h - config.borderwidth, .width = config.titlewidth + 1, .height = 1};
			recs[4] = (XRectangle){.x = x + 0, .y = c->h - config.borderwidth, .width = 1, .height = config.borderwidth - 1};
			recs[5] = (XRectangle){.x = x + 1, .y = 0, .width = 1, .height = config.borderwidth - 2};
			recs[6] = (XRectangle){.x = x + 0, .y = 1, .width = config.corner - 2, .height = 1};
			recs[7] = (XRectangle){.x = x + config.titlewidth + 1, .y = config.borderwidth - 2, .width = 1, .height = c->h - config.borderwidth * 2 + 4};
			recs[8] = (XRectangle){.x = x + 1, .y = c->h - config.borderwidth + 1, .width = config.titlewidth + 1, .height = 1};
			recs[9] = (XRectangle){.x = x + 1, .y = c->h - config.borderwidth + 1, .width = 1, .height = config.borderwidth - 3};
			val.foreground = (o == NE) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 10 : 5);
			recs[0] = (XRectangle){.x = x + config.corner - 1, .y = 0, .width = 1, .height = c->h};
			recs[1] = (XRectangle){.x = x + 0, .y = config.borderwidth - 1, .width = config.titlewidth, .height = 1};
			recs[2] = (XRectangle){.x = x + 0, .y = c->h - 1, .width = config.corner, .height = 1};
			recs[3] = (XRectangle){.x = x + config.corner - 2, .y = 1, .width = 1, .height = c->h - 1};
			recs[4] = (XRectangle){.x = x + 1, .y = config.borderwidth - 2, .width = config.titlewidth, .height = 1};
			recs[5] = (XRectangle){.x = x + 1, .y = c->h - 2, .width = config.corner - 1, .height = 1};
			val.foreground = (o == NE) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 6 : 3);
		} else {
			/* top left corner */
			x = y = 0;
			recs[0] = (XRectangle){.x = x + 0, .y = y + 0, .width = 1, .height = config.corner - 1};
			recs[1] = (XRectangle){.x = x + 0, .y = y + 0, .width = config.corner - 1, .height = 1};
			recs[2] = (XRectangle){.x = x + 1, .y = y + 0, .width = 1, .height = config.corner - 2};
			recs[3] = (XRectangle){.x = x + 0, .y = y + 1, .width = config.corner - 2, .height = 1};
			val.foreground = (o == NW) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 4: 2);
			recs[0] = (XRectangle){.x = x + config.borderwidth - 1, .y = y + config.borderwidth - 1, .width = 1, .height = config.titlewidth + 1};
			recs[1] = (XRectangle){.x = x + config.borderwidth - 1, .y = y + config.borderwidth - 1, .width = config.titlewidth + 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.corner - 1, .y = y, .width = 1, .height = config.borderwidth};
			recs[3] = (XRectangle){.x = x, .y = y + config.corner - 1, .width = config.borderwidth, .height = 1};
			recs[4] = (XRectangle){.x = x + config.borderwidth - 2, .y = y + config.borderwidth - 2, .width = 1, .height = config.titlewidth + 2};
			recs[5] = (XRectangle){.x = x + config.borderwidth - 2, .y = y + config.borderwidth - 2, .width = config.titlewidth + 2, .height = 1};
			recs[6] = (XRectangle){.x = x + config.corner - 2, .y = y + 1, .width = 1, .height = config.borderwidth - 1};
			recs[7] = (XRectangle){.x = x + 1, .y = y + config.corner - 2, .width = config.borderwidth - 1, .height = 1};
			val.foreground = (o == NW) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 8 : 4);

			/* bottom left corner */
			x = 0;
			y = c->h - config.corner;
			recs[0] = (XRectangle){.x = x + 0, .y = y + 0, .width = 1, .height = config.corner - 1};
			recs[1] = (XRectangle){.x = x + 0, .y = y + 0, .width = config.borderwidth - 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.borderwidth - 1, .y = y + config.titlewidth, .width = config.titlewidth, .height = 1};
			recs[3] = (XRectangle){.x = x + 1, .y = y + 0, .width = 1, .height = config.corner - 2};
			recs[4] = (XRectangle){.x = x + 0, .y = y + 1, .width = config.borderwidth - 2, .height = 1};
			recs[5] = (XRectangle){.x = x + config.borderwidth - 2, .y = y + config.titlewidth + 1, .width = config.titlewidth, .height = 1};
			val.foreground = (o == SW) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 6 : 3);
			recs[0] = (XRectangle){.x = x + config.borderwidth - 1, .y = y + 0, .width = 1, .height = config.titlewidth};
			recs[1] = (XRectangle){.x = x + 0, .y = y + config.corner - 1, .width = config.corner, .height = 1};
			recs[2] = (XRectangle){.x = x + config.corner - 1, .y = y + config.titlewidth, .width = 1, .height = config.borderwidth};
			recs[3] = (XRectangle){.x = x + config.borderwidth - 2, .y = y + 1, .width = 1, .height = config.titlewidth};
			recs[4] = (XRectangle){.x = x + 1, .y = y + config.corner - 2, .width = config.corner - 1, .height = 1};
			recs[5] = (XRectangle){.x = x + config.corner - 2, .y = y + config.titlewidth + 1, .width = 1, .height = config.borderwidth - 1};
			val.foreground = (o == SW) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 6 : 3);

			/* top right corner */
			x = c->w - config.corner;
			y = 0;
			recs[0] = (XRectangle){.x = x + 0, .y = y + 0, .width = 1, .height = config.borderwidth - 1};
			recs[1] = (XRectangle){.x = x + 0, .y = y + 0, .width = config.corner - 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.titlewidth, .y = y + config.borderwidth - 1, .width = 1, .height = config.titlewidth};
			recs[3] = (XRectangle){.x = x + 1, .y = y + 0, .width = 1, .height = config.borderwidth - 2};
			recs[4] = (XRectangle){.x = x + 0, .y = y + 1, .width = config.corner - 2, .height = 1};
			recs[5] = (XRectangle){.x = x + config.titlewidth + 1, .y = y + config.borderwidth - 2, .width = 1, .height = config.titlewidth};
			val.foreground = (o == NE) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 6 : 3);
			recs[0] = (XRectangle){.x = x + config.corner - 1, .y = y + 0, .width = 1, .height = config.corner};
			recs[1] = (XRectangle){.x = x + 0, .y = y + config.borderwidth - 1, .width = config.titlewidth, .height = 1};
			recs[2] = (XRectangle){.x = x + config.titlewidth, .y = y + config.corner - 1, .width = config.borderwidth, .height = 1};
			recs[3] = (XRectangle){.x = x + config.corner - 2, .y = y + 1, .width = 1, .height = config.corner};
			recs[4] = (XRectangle){.x = x + 1, .y = y + config.borderwidth - 2, .width = config.titlewidth, .height = 1};
			recs[5] = (XRectangle){.x = x + config.titlewidth + 1, .y = y + config.corner - 2, .width = config.borderwidth - 1, .height = 1};
			val.foreground = (o == NE) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 6 : 3);

			/* bottom right corner */
			x = c->w - config.corner;
			y = c->h - config.corner;
			recs[0] = (XRectangle){.x = x + 0, .y = y + config.titlewidth, .width = 1, .height = config.borderwidth - 1};
			recs[1] = (XRectangle){.x = x + config.titlewidth, .y = y + 0, .width = config.borderwidth - 1, .height = 1};
			recs[2] = (XRectangle){.x = x + config.titlewidth, .y = y + 0, .width = 1, .height = config.titlewidth + 1};
			recs[3] = (XRectangle){.x = x + 0, .y = y + config.titlewidth, .width = config.titlewidth + 1, .height = 1};
			recs[4] = (XRectangle){.x = x + 1, .y = y + config.titlewidth + 1, .width = 1, .height = config.borderwidth - 3};
			recs[5] = (XRectangle){.x = x + config.titlewidth + 1, .y = y + 1, .width = config.borderwidth - 3, .height = 1};
			recs[6] = (XRectangle){.x = x + config.titlewidth + 1, .y = y + 1, .width = 1, .height = config.titlewidth + 1};
			recs[7] = (XRectangle){.x = x + 1, .y = y + config.titlewidth + 1, .width = config.titlewidth + 1, .height = 1};
			val.foreground = (o == SE) ? decor[COLOR_DARK] : decor[COLOR_LIGHT];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 8 : 4);
			recs[0] = (XRectangle){.x = x + config.corner - 1, .y = y + 0, .width = 1, .height = config.corner};
			recs[1] = (XRectangle){.x = x + 0, .y = y + config.corner - 1, .width = config.corner, .height = 1};
			recs[2] = (XRectangle){.x = x + config.corner - 2, .y = y + 1, .width = 1, .height = config.corner - 1};
			recs[3] = (XRectangle){.x = x + 1, .y = y + config.corner - 2, .width = config.corner - 1, .height = 1};
			val.foreground = (o == SE) ? decor[COLOR_LIGHT] : decor[COLOR_DARK];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangles(dpy, c->pix, gc, recs, doubleshadow ? 4 : 2);
		}
	}

	for (col = c->cols; col != NULL; col = col->next) {
		/* draw column division */
		if (col->next != NULL) {
			drawrectangle(c->pix, doubleshadow, col->x + col->w, c->b, config.divwidth, c->h - 2 * c->b,
			              (col == cdiv ? decor[COLOR_DARK] : decor[COLOR_LIGHT]),
			              (col == cdiv ? decor[COLOR_LIGHT] : decor[COLOR_DARK]));
		}

		for (row = col->rows; row != NULL; row = row->next) {
			/* draw row division */
			if (!isshaded && col->maxrow == NULL && row->next != NULL) {
				drawrectangle(c->pix, doubleshadow, col->x, row->y + row->h, col->w, config.divwidth,
				              (row == rdiv ? decor[COLOR_DARK] : decor[COLOR_LIGHT]),
				              (row == rdiv ? decor[COLOR_LIGHT] : decor[COLOR_DARK]));
			}

			/* (re)create titlebar pixmap */
			if (row->pw != col->w || row->pixbar == None) {
				if (row->pixbar != None)
					XFreePixmap(dpy, row->pixbar);
				row->pixbar = XCreatePixmap(dpy, row->bar, col->w, config.titlewidth, depth);
			}
			row->pw = col->w;


			/* draw background of titlebar pixmap */
			val.foreground = decor[COLOR_MID];
			XChangeGC(dpy, gc, GCForeground, &val);
			XFillRectangle(dpy, row->pixbar, gc, 0, 0, col->w, config.titlewidth);
			XCopyArea(dpy, row->pixbar, row->bar, gc, 0, 0, col->w, config.titlewidth, 0, 0);

			/* draw buttons */
			buttonleftdecorate(row, 0);
			buttonrightdecorate(row, 0);

			/* decorate tabs, if necessary */
			if (recursive) {
				for (t = row->tabs; t != NULL; t = t->next) {
					tabdecorate(t, 0);
					for (d = t->ds; d != NULL; d = d->next) {
						dialogdecorate(d);
					}
				}
			}
		}
	}

	XCopyArea(dpy, c->pix, c->frame, gc, 0, 0, c->w, c->h, 0, 0);
}

/* decorate prompt frame */
static void
promptdecorate(struct Prompt *prompt, int w, int h)
{
	XGCValues val;
	XRectangle recs[10];
	int partw, parth;
	int doubleshadow;

	if (prompt->pw == w && prompt->ph == h && prompt->pix != None)
		goto done;

	/* (re)create pixmap */
	if (prompt->pix != None)
		XFreePixmap(dpy, prompt->pix);
	prompt->pix = XCreatePixmap(dpy, prompt->frame, w, h, depth);
	prompt->pw = w;
	prompt->ph = h;

	partw = w - 2 * config.borderwidth;
	parth = h - 2 * config.borderwidth;
	doubleshadow = (config.borderwidth - 4 > 0);

	/* draw background */
	val.foreground = visual.prompt[COLOR_MID];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangle(dpy, prompt->pix, gc, 0, 0, w, h);

	/* draw light shadow */
	recs[0] = (XRectangle){.x = 0, .y = 0, .width = 1, .height = h - 1};
	recs[1] = (XRectangle){.x = 0, .y = 0, .width = config.borderwidth - 1, .height = 1};
	recs[2] = (XRectangle){
		.x = w - config.borderwidth,
		.y = 0,
		.width = 1,
		.height = parth + config.borderwidth,
	};
	recs[3] = (XRectangle){
		.x = config.borderwidth - 1,
		.y = h - config.borderwidth,
		.width = partw + 2,
		.height = 1
	};
	recs[4] = (XRectangle){.x = 1, .y = 1, .width = 1, .height = h - 2};
	recs[5] = (XRectangle){.x = 0, .y = 1, .width = config.borderwidth - 2, .height = 1};
	recs[6] = (XRectangle){
		.x = w - config.borderwidth + 1,
		.y = 0,
		.width = 1,
		.height = parth + config.borderwidth + 1,
	};
	recs[7] = (XRectangle){
		.x = config.borderwidth - 2,
		.y = h - config.borderwidth + 1,
		.width = partw + 4,
		.height = 1
	};
	recs[8] = (XRectangle){.x = w - config.borderwidth, .y = 0, .width = config.borderwidth - 1, .height = 1};
	recs[9] = (XRectangle){.x = w - config.borderwidth, .y = 1, .width = config.borderwidth - 2, .height = 1};
	val.foreground = visual.prompt[COLOR_LIGHT];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, prompt->pix, gc, recs, doubleshadow ? 10 : 5);

	/* draw dark shadow */
	recs[0] = (XRectangle){.x = w - 1, .y = 0, .width = 1, .height = h};
	recs[1] = (XRectangle){.x = 0, .y = h - 1, .width = w, .height = 1};
	recs[2] = (XRectangle){
		.x = config.borderwidth - 1,
		.y = 0,
		.width = 1,
		.height = parth + config.borderwidth,
	};
	recs[3] = (XRectangle){.x = w - 2, .y = 1, .width = 1, .height = h - 2};
	recs[4] = (XRectangle){.x = 1, .y = h - 2, .width = w - 2, .height = 1};
	recs[5] = (XRectangle){
		.x = config.borderwidth - 2,
		.y = 1,
		.width = 1,
		.height = parth + config.borderwidth,
	};
	val.foreground = visual.prompt[COLOR_DARK];
	XChangeGC(dpy, gc, GCForeground, &val);
	XFillRectangles(dpy, prompt->pix, gc, recs, doubleshadow ? 6 : 3);

done:
	XCopyArea(dpy, prompt->pix, prompt->frame, gc, 0, 0, w, h, 0, 0);
}

/* decorate notification */
static void
notifdecorate(struct Notification *n)
{
	/* (re)create pixmap */
	if (n->pw != n->w || n->ph != n->h || n->pix == None) {
		if (n->pix != None)
			XFreePixmap(dpy, n->pix);
		n->pix = XCreatePixmap(dpy, n->frame, n->w, n->h, depth);
	}
	n->pw = n->w;
	n->ph = n->h;

	drawborders(n->pix, (config.borderwidth - 4 > 0), n->w, n->h, visual.notif);

	XCopyArea(dpy, n->pix, n->frame, gc, 0, 0, n->w, n->h, 0, 0);
}

/* remove container from the focus list */
static void
containerdelfocus(struct Container *c)
{
	if (c->fnext != NULL) {
		c->fnext->fprev = c->fprev;
	}
	if (c->fprev != NULL) {
		c->fprev->fnext = c->fnext;
	} else if (wm.focuslist == c) {
		wm.focuslist = c->fnext;
	}
	c->fnext = NULL;
	c->fprev = NULL;
}

/* put container on beginning of focus list */
static void
containeraddfocus(struct Container *c)
{
	if (c == NULL || c->isminimized)
		return;
	containerdelfocus(c);
	c->fnext = wm.focuslist;
	c->fprev = NULL;
	if (wm.focuslist)
		wm.focuslist->fprev = c;
	wm.focuslist = c;
}

/* hide container */
static void
containerhide(struct Container *c, int hide)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;

	if (c == NULL)
		return;
	c->ishidden = hide;
	if (hide)
		XUnmapWindow(dpy, c->frame);
	else
		XMapWindow(dpy, c->frame);
	for (col = c->cols; col != NULL; col = col->next) {
		for (row = col->rows; row != NULL; row = row->next) {
			for (t = row->tabs; t != NULL; t = t->next) {
				icccmwmstate(t->win, (hide ? IconicState : NormalState));
				for (d = t->ds; d != NULL; d = d->next) {
					icccmwmstate(d->win, (hide ? IconicState : NormalState));
				}
			}
		}
	}
}

/* commit dialog size and position */
static void
dialogmoveresize(struct Dialog *d)
{
	struct Container *c;
	int dx, dy, dw, dh;

	c = d->t->row->col->c;
	dx = d->x - config.borderwidth;
	dy = d->y - config.borderwidth;
	dw = d->w + 2 * config.borderwidth;
	dh = d->h + 2 * config.borderwidth;
	XMoveResizeWindow(dpy, d->frame, dx, dy, dw, dh);
	XMoveResizeWindow(dpy, d->win, config.borderwidth, config.borderwidth, d->w, d->h);
	winnotify(d->win, c->x + d->t->row->col->x + d->x, c->y + d->t->row->y + d->y, d->w, d->h);
	if (d->pw != dw || d->ph != dh) {
		dialogdecorate(d);
	}
}

/* configure dialog window */
static void
dialogconfigure(struct Dialog *d, unsigned int valuemask, XWindowChanges *wc)
{
	if (d == NULL)
		return;
	if (valuemask & CWWidth)
		d->maxw = wc->width;
	if (valuemask & CWHeight)
		d->maxh = wc->height;
	dialogmoveresize(d);
}

/* commit tab size and position */
static void
tabmoveresize(struct Tab *t)
{
	XMoveResizeWindow(dpy, t->title, t->x, 0, t->w, config.titlewidth);
	if (t->ptw != t->w) {
		tabdecorate(t, 0);
	}
}

/* commit titlebar size and position */
static void
titlebarmoveresize(struct Row *row, int x, int y, int w)
{
	XMoveResizeWindow(dpy, row->bar, x, y, w, config.titlewidth);
	XMoveWindow(dpy, row->bl, 0, 0);
	XMoveWindow(dpy, row->br, w - config.titlewidth, 0);
}

/* commit container size and position */
static void
containermoveresize(struct Container *c)
{
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	int rowy, rowh;
	int isshaded;

	if (c == NULL)
		return;
	XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
	XMoveResizeWindow(dpy, c->curswin[BORDER_N], config.corner, 0, c->w - 2 * config.corner, c->b);
	XMoveResizeWindow(dpy, c->curswin[BORDER_S], config.corner, c->h - c->b, c->w - 2 * config.corner, c->b);
	XMoveResizeWindow(dpy, c->curswin[BORDER_W], 0, config.corner, c->b, c->h - 2 * config.corner);
	XMoveResizeWindow(dpy, c->curswin[BORDER_E], c->w - c->b, config.corner, c->b, c->h - 2 * config.corner);
	XMoveResizeWindow(dpy, c->curswin[BORDER_NW], 0, 0, config.corner, config.corner);
	XMoveResizeWindow(dpy, c->curswin[BORDER_NE], c->w - config.corner, 0, config.corner, config.corner);
	XMoveResizeWindow(dpy, c->curswin[BORDER_SW], 0, c->h - config.corner, config.corner, config.corner);
	XMoveResizeWindow(dpy, c->curswin[BORDER_SE], c->w - config.corner, c->h - config.corner, config.corner, config.corner);
	isshaded = containerisshaded(c);
	for (col = c->cols; col != NULL; col = col->next) {
		rowy = c->b;
		rowh = max(1, c->h - 2 * c->b - col->nrows * config.titlewidth);
		if (col->next != NULL) {
			XMoveResizeWindow(dpy, col->div, col->x + col->w, c->b, config.divwidth, c->h - 2 * c->b);
			XMapWindow(dpy, col->div);
		} else {
			XUnmapWindow(dpy, col->div);
		}
		for (row = col->rows; row != NULL; row = row->next) {
			if (!isshaded && row->next != NULL && col->maxrow == NULL) {
				XMoveResizeWindow(dpy, row->div, col->x, row->y + row->h, col->w, config.divwidth);
				XMapWindow(dpy, row->div);
			} else {
				XUnmapWindow(dpy, row->div);
			}
			if (!isshaded && col->maxrow == NULL) {              /* regular row */
				titlebarmoveresize(row, col->x, row->y, col->w);
				XMoveResizeWindow(dpy, row->frame, col->x, row->y + config.titlewidth, col->w, row->h - config.titlewidth);
				XMapWindow(dpy, row->frame);
			} else if (!isshaded && row == col->maxrow) {        /* maximized row */
				titlebarmoveresize(row, col->x, rowy, col->w);
				XMoveResizeWindow(dpy, row->frame, col->x, rowy + config.titlewidth, col->w, rowh);
				XMapWindow(dpy, row->frame);
				rowy += rowh;
			} else {                                /* minimized row */
				titlebarmoveresize(row, col->x, rowy, col->w);
				XUnmapWindow(dpy, row->frame);
			}
			rowy += config.titlewidth;
			for (t = row->tabs; t != NULL; t = t->next) {
				XMoveResizeWindow(dpy, t->frame, 0, 0, t->winw, t->winh);
				for (d = t->ds; d != NULL; d = d->next) {
					dialogmoveresize(d);
				}
				XResizeWindow(dpy, t->win, t->winw, t->winh);
				winnotify(t->win, c->x + col->x, c->y + row->y + config.titlewidth, t->winw, t->winh);
				ewmhsetframeextents(t->win, c->b, (c->isfullscreen && c->ncols == 1 && c->cols->nrows == 1) ? 0 : config.titlewidth);
				tabmoveresize(t);
			}
		}
	}
}

/* check if container needs to be redecorated and redecorate it */
static void
containerredecorate(struct Container *c, struct Column *cdiv, struct Row *rdiv, enum Octant o)
{
	if (c->pw != c->w || c->ph != c->h) {
		containerdecorate(c, cdiv, rdiv, 0, o);
	}
}

/* configure container size and position */
static void
containerconfigure(struct Container *c, unsigned int valuemask, XWindowChanges *wc)
{
	if (c == NULL || c->isminimized || c->isfullscreen || c->ismaximized)
		return;
	if (valuemask & CWX)
		c->nx = wc->x;
	if (valuemask & CWY)
		c->ny = wc->y;
	if ((valuemask & CWWidth) && wc->width >= wm.minsize)
		c->nw = wc->width;
	if ((valuemask & CWHeight) && wc->height >= wm.minsize)
		c->nh = wc->height;
	containercalccols(c, 0, 1);
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
}

/* remove container from the raise list */
static void
containerdelraise(struct Container *c)
{
	if (c->rnext != NULL) {
		c->rnext->rprev = c->rprev;
	}
	if (c->rprev != NULL) {
		c->rprev->rnext = c->rnext;
	} else if (wm.fulllist == c) {
		wm.fulllist = c->rnext;
	} else if (wm.abovelist == c) {
		wm.abovelist = c->rnext;
	} else if (wm.centerlist == c) {
		wm.centerlist = c->rnext;
	} else if (wm.belowlist == c) {
		wm.belowlist = c->rnext;
	}
	c->rnext = NULL;
	c->rprev = NULL;
}

/* put container on beginning of a raise list */
static void
containeraddraise(struct Container *c)
{
	struct Container **list;

	containerdelraise(c);
	if (c->isfullscreen)
		list = &wm.fulllist;
	else if (c->layer > 0)
		list = &wm.abovelist;
	else if (c->layer < 0)
		list = &wm.belowlist;
	else
		list = &wm.centerlist;
	c->rnext = *list;
	c->rprev = NULL;
	if (*list != NULL)
		(*list)->rprev = c;
	*list = c;
}

/* raise container */
static void
containerraise(struct Container *c)
{
	Window wins[2];

	if (c == NULL || c->isminimized)
		return;
	containeraddraise(c);
	wins[1] = c->frame;
	if (c->isfullscreen)
		wins[0] = wm.layerwins[LAYER_FULLSCREEN];
	else if (c->layer > 0)
		wins[0] = wm.layerwins[LAYER_ABOVE];
	else if (c->layer < 0)
		wins[0] = wm.layerwins[LAYER_BELOW];
	else
		wins[0] = wm.layerwins[LAYER_NORMAL];
	XRestackWindows(dpy, wins, 2);
	ewmhsetclientsstacking();
}

/* send container to desktop, raise it and optionally place it */
static void
containersendtodesk(struct Container *c, struct Desktop *desk, int place, int userplaced)
{
	if (c == NULL || desk == NULL || c->isminimized)
		return;
	c->desk = desk;
	c->mon = desk->mon;
	if (c->issticky) {
		c->issticky = 0;
		ewmhsetstate(c);
	}
	if (place)
		containerplace(c, c->desk, userplaced);
	if (desk != desk->mon->seldesk)  /* container was sent to invisible desktop */
		containerhide(c, 1);
	containerraise(c);
	ewmhsetwmdesktop(c);
}

/* minimize container; optionally focus another container */
static void
containerminimize(struct Container *c, int minimize, int focus)
{
	void tabfocus(struct Tab *, int);
	struct Container *tofocus;

	if (minimize != REMOVE && !c->isminimized) {
		c->isminimized = 1;
		containerhide(c, 1);
		containerdelfocus(c);
		if (focus) {
			if ((tofocus = getnextfocused(c->mon, c->desk)) != NULL) {
				tabfocus(tofocus->selcol->selrow->seltab, 0);
			} else {
				tabfocus(NULL, 0);
			}
		}
	} else if (minimize != ADD && c->isminimized) {
		c->isminimized = 0;
		containersendtodesk(c, wm.selmon->seldesk, 1, 0);
		containermoveresize(c);
		containerhide(c, 0);
		tabfocus(c->selcol->selrow->seltab, 0);
	} else {
		return;
	}
	ewmhsetstate(c);
}

/* make a container occupy the whole monitor */
static void
containerfullscreen(struct Container *c, int fullscreen)
{
	if (fullscreen != REMOVE && !c->isfullscreen)
		c->isfullscreen = 1;
	else if (fullscreen != ADD && c->isfullscreen)
		c->isfullscreen = 0;
	else
		return;
	containerraise(c);
	containercalccols(c, 0, 1);
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
	ewmhsetstate(c);
}

/* maximize a container on the monitor */
static void
containermaximize(struct Container *c, int maximize)
{
	if (maximize != REMOVE && !c->ismaximized)
		c->ismaximized = 1;
	else if (maximize != ADD && c->ismaximized)
		c->ismaximized = 0;
	else
		return;
	containercalccols(c, 0, 1);
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
	ewmhsetstate(c);
}

/* shade container title bar */
static void
containershade(struct Container *c, int shade)
{
	void tabfocus(struct Tab *t, int gotodesk);

	if (shade != REMOVE && !c->isshaded) {
		c->isshaded = 1;
		XDefineCursor(dpy, c->curswin[BORDER_NW], visual.cursors[CURSOR_W]);
		XDefineCursor(dpy, c->curswin[BORDER_SW], visual.cursors[CURSOR_W]);
		XDefineCursor(dpy, c->curswin[BORDER_NE], visual.cursors[CURSOR_E]);
		XDefineCursor(dpy, c->curswin[BORDER_SE], visual.cursors[CURSOR_E]);
	} else if (shade != ADD && c->isshaded) {
		c->isshaded = 0;
		XDefineCursor(dpy, c->curswin[BORDER_NW], visual.cursors[CURSOR_NW]);
		XDefineCursor(dpy, c->curswin[BORDER_SW], visual.cursors[CURSOR_SW]);
		XDefineCursor(dpy, c->curswin[BORDER_NE], visual.cursors[CURSOR_NE]);
		XDefineCursor(dpy, c->curswin[BORDER_SE], visual.cursors[CURSOR_SE]);
	} else {
		return;
	}
	containercalccols(c, 0, 1);
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
	ewmhsetstate(c);
	if (c == wm.focused) {
		tabfocus(c->selcol->selrow->seltab, 0);
	}
}

/* stick a container on the monitor */
static void
containerstick(struct Container *c, int stick)
{
	if (stick != REMOVE && !c->issticky) {
		c->issticky = 1;
	} else if (stick != ADD && c->issticky) {
		c->issticky = 0;
		containersendtodesk(c, c->mon->seldesk, 0, 0);
	} else {
		return;
	}
	ewmhsetstate(c);
}

/* raise container above others */
static void
containerabove(struct Container *c, int above)
{
	if (above != REMOVE && c->layer != 1)
		c->layer = 1;
	else if (above != ADD && c->layer != 0)
		c->layer = 0;
	else
		return;
	containerraise(c);
	ewmhsetstate(c);
}

/* lower container below others */
static void
containerbelow(struct Container *c, int below)
{
	if (below != REMOVE && c->layer != -1)
		c->layer = -1;
	else if (below != ADD && c->layer != 0)
		c->layer = 0;
	else
		return;
	containerraise(c);
	ewmhsetstate(c);
}

/* create new container */
static struct Container *
containernew(int x, int y, int w, int h)
{
	struct Container *c;
	int i;

	c = emalloc(sizeof *c);
	c->prev = c->next = NULL;
	c->fprev = c->fnext = NULL;
	c->rprev = c->rnext = NULL;
	c->mon = NULL;
	c->cols = NULL;
	c->selcol = NULL;
	c->ncols = 0;
	c->isfullscreen = 0;
	c->ismaximized = 0;
	c->isminimized = 0;
	c->issticky = 0;
	c->isshaded = 0;
	c->ishidden = 0;
	c->layer = 0;
	c->desk = NULL;
	c->pw = c->ph = 0;
	c->x = c->nx = x - config.borderwidth;
	c->y = c->ny = y - config.borderwidth;
	c->w = c->nw = w + 2 * config.borderwidth;
	c->h = c->nh = h + 2 * config.borderwidth + config.titlewidth;
	c->b = config.borderwidth;
	c->pix = None;
	c->frame = XCreateWindow(dpy, root, c->x, c->y, c->w, c->h, 0,
	                         CopyFromParent, CopyFromParent, CopyFromParent,
	                         CWEventMask, &clientswa);

	c->curswin[BORDER_N] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_N]});
	c->curswin[BORDER_S] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_S]});
	c->curswin[BORDER_W] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_W]});
	c->curswin[BORDER_E] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_E]});
	c->curswin[BORDER_NW] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_NW]});
	c->curswin[BORDER_NE] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_NE]});
	c->curswin[BORDER_SW] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_SW]});
	c->curswin[BORDER_SE] = XCreateWindow(dpy, c->frame, 0, 0, 1, 1, 0, CopyFromParent, InputOnly, CopyFromParent, CWCursor, &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_SE]});
	for (i = 0; i < BORDER_LAST; i++)
		XMapWindow(dpy, c->curswin[i]);
	if (wm.c)
		wm.c->prev = c;
	c->next = wm.c;
	wm.c = c;
	containeraddfocus(c);
	return c;
}

/* delete dialog */
static void
dialogdel(struct Dialog *d)
{
	if (d->next)
		d->next->prev = d->prev;
	if (d->prev)
		d->prev->next = d->next;
	else
		d->t->ds = d->next;
	if (d->pix != None)
		XFreePixmap(dpy, d->pix);
	icccmdeletestate(d->win);
	XReparentWindow(dpy, d->win, root, 0, 0);
	XDestroyWindow(dpy, d->frame);
	clientsdecr();
	free(d);
}

/* detach tab from row */
static void
tabdetach(struct Tab *t, int x, int y, int w, int h)
{
	struct Row *row;

	row = t->row;
	if (row->seltab == t)
		row->seltab = (t->prev != NULL) ? t->prev : t->next;
	row->ntabs--;
	t->ignoreunmap = IGNOREUNMAP;
	XReparentWindow(dpy, t->title, root, x, y);
	if (t->next)
		t->next->prev = t->prev;
	if (t->prev)
		t->prev->next = t->next;
	else
		row->tabs = t->next;
	t->winw = w;
	t->winh = h;
	t->next = NULL;
	t->prev = NULL;
	t->row = NULL;
	rowcalctabs(row);
}

/* delete tab */
static void
tabdel(struct Tab *t)
{
	while (t->ds)
		dialogdel(t->ds);
	tabdetach(t, 0, 0, t->winw, t->winh);
	if (t->pixtitle != None)
		XFreePixmap(dpy, t->pixtitle);
	if (t->pix != None)
		XFreePixmap(dpy, t->pix);
	icccmdeletestate(t->win);
	XReparentWindow(dpy, t->win, root, 0, 0);
	XDestroyWindow(dpy, t->title);
	XDestroyWindow(dpy, t->frame);
	clientsdecr();
	free(t->name);
	free(t);
}

/* stack rows */
static void
rowstack(struct Column *col, struct Row *row)
{
	if (row == NULL) {
		col->maxrow = NULL;
	} else if (col->maxrow != row) {
		col->maxrow = row;
		rowcalctabs(row);
	} else {
		return;
	}
	containermoveresize(col->c);
	containerdecorate(col->c, NULL, NULL, 0, 0);
}

/* detach row from column */
static void
rowdetach(struct Row *row, int recalc)
{
	if (row->col->selrow == row)
		row->col->selrow = (row->prev != NULL) ? row->prev : row->next;
	row->col->nrows--;
	if (row->next)
		row->next->prev = row->prev;
	if (row->prev)
		row->prev->next = row->next;
	else
		row->col->rows = row->next;
	if (row == row->col->maxrow)
		row->col->maxrow = NULL;
	row->next = NULL;
	row->prev = NULL;
	if (recalc) {
		colcalcrows(row->col, 1, 0);
	}
}

/* delete row */
static void
rowdel(struct Row *row)
{
	while (row->tabs)
		tabdel(row->tabs);
	rowdetach(row, 1);
	XDestroyWindow(dpy, row->frame);
	XDestroyWindow(dpy, row->bar);
	XDestroyWindow(dpy, row->bl);
	XDestroyWindow(dpy, row->br);
	XDestroyWindow(dpy, row->div);
	if (row->pixbar != None)
		XFreePixmap(dpy, row->pixbar);
	XFreePixmap(dpy, row->pixbl);
	XFreePixmap(dpy, row->pixbr);
	free(row);
}

/* detach column from container */
static void
coldetach(struct Column *col)
{
	if (col->c->selcol == col)
		col->c->selcol = (col->prev != NULL) ? col->prev : col->next;
	col->c->ncols--;
	if (col->next)
		col->next->prev = col->prev;
	if (col->prev)
		col->prev->next = col->next;
	else
		col->c->cols = col->next;
	col->next = NULL;
	col->prev = NULL;
	containercalccols(col->c, 1, 0);
}

/* delete column */
static void
coldel(struct Column *col)
{
	while (col->rows)
		rowdel(col->rows);
	coldetach(col);
	XDestroyWindow(dpy, col->div);
	free(col);
}

/* delete container */
static void
containerdel(struct Container *c)
{
	int i;

	containerdelfocus(c);
	containerdelraise(c);
	if (wm.focused == c)
		wm.focused = NULL;
	if (c->next)
		c->next->prev = c->prev;
	if (c->prev)
		c->prev->next = c->next;
	else
		wm.c = c->next;
	while (c->cols)
		coldel(c->cols);
	if (c->pix != None)
		XFreePixmap(dpy, c->pix);
	XDestroyWindow(dpy, c->frame);
	for (i = 0; i < BORDER_LAST; i++)
		XDestroyWindow(dpy, c->curswin[i]);
	free(c);
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
	if (prev == NULL || c->cols == NULL) {
		col->prev = NULL;
		col->next = c->cols;
		if (c->cols != NULL)
			c->cols->prev = col;
		c->cols = col;
	} else {
		if (prev->next != NULL)
			prev->next->prev = col;
		col->next = prev->next;
		col->prev = prev;
		prev->next = col;
	}
	XReparentWindow(dpy, col->div, c->frame, 0, 0);
	containercalccols(c, 1, 0);
	if (oldc != NULL && oldc->ncols == 0) {
		containerdel(oldc);
	}
}

/* create new column */
static struct Column *
colnew(void)
{
	struct Column *col;

	col = emalloc(sizeof(*col));
	col->prev = col->next = NULL;
	col->c = NULL;
	col->rows = NULL;
	col->selrow = NULL;
	col->maxrow = NULL;
	col->nrows = 0;
	col->x = 0;
	col->w = 0;
	col->fact = 0.0;
	col->div = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                         CopyFromParent, InputOnly, CopyFromParent, CWCursor,
	                         &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_H]});
	return col;
}

/* add row to column */
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
	if (prev == NULL || col->rows == NULL) {
		row->prev = NULL;
		row->next = col->rows;
		if (col->rows != NULL)
			col->rows->prev = row;
		col->rows = row;
	} else {
		if (prev->next)
			prev->next->prev = row;
		row->next = prev->next;
		row->prev = prev;
		prev->next = row;
	}
	colcalcrows(col, 1, 0);    /* set row->y, row->h, etc */
	XReparentWindow(dpy, row->div, c->frame, col->x + col->w, c->b);
	XReparentWindow(dpy, row->bar, c->frame, col->x, row->y);
	XReparentWindow(dpy, row->frame, c->frame, col->x, row->y);
	XMapWindow(dpy, row->bar);
	XMapWindow(dpy, row->frame);
	if (oldcol != NULL && oldcol->nrows == 0) {
		coldel(oldcol);
	}
}

/* create new row */
static struct Row *
rownew(void)
{
	struct Row *row;

	row = emalloc(sizeof(*row));
	row->prev = row->next = NULL;
	row->col = NULL;
	row->tabs = NULL;
	row->seltab = NULL;
	row->ntabs = 0;
	row->y = 0;
	row->h = 0;
	row->pw = 0;
	row->frame = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                           CopyFromParent, CopyFromParent, CopyFromParent,
	                           CWEventMask, &clientswa);
	row->bar = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                         CopyFromParent, CopyFromParent, CopyFromParent,
	                         CWEventMask, &clientswa);
	row->bl = XCreateWindow(dpy, row->bar, 0, 0, config.titlewidth, config.titlewidth, 0,
	                        CopyFromParent, CopyFromParent, CopyFromParent,
	                        CWEventMask, &clientswa);
	row->pixbl = XCreatePixmap(dpy, row->bl, config.titlewidth, config.titlewidth, depth);
	row->br = XCreateWindow(dpy, row->bar, 0, 0, config.titlewidth, config.titlewidth, 0,
	                        CopyFromParent, CopyFromParent, CopyFromParent,
	                        CWEventMask, &clientswa);
	row->div = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                         CopyFromParent, InputOnly, CopyFromParent, CWCursor,
	                         &(XSetWindowAttributes){.cursor = visual.cursors[CURSOR_V]});
	row->pixbr = XCreatePixmap(dpy, row->bl, config.titlewidth, config.titlewidth, depth);
	row->pixbar = None;
	XMapWindow(dpy, row->bl);
	XMapWindow(dpy, row->br);
	XDefineCursor(dpy, row->br, visual.cursors[CURSOR_PIRATE]);
	return row;
}

/* add tab to row */
static void
rowaddtab(struct Row *row, struct Tab *t, struct Tab *prev)
{
	struct Container *c;
	struct Column *col;
	struct Row *oldrow;

	c = row->col->c;
	col = row->col;
	oldrow = t->row;
	t->row = row;
	row->seltab = t;
	row->ntabs++;
	if (prev == NULL || row->tabs == NULL) {
		t->prev = NULL;
		t->next = row->tabs;
		if (row->tabs != NULL)
			row->tabs->prev = t;
		row->tabs = t;
	} else {
		if (prev->next)
			prev->next->prev = t;
		t->next = prev->next;
		t->prev = prev;
		prev->next = t;
	}
	rowcalctabs(row);               /* set t->x, t->w, etc */
	if (t->title == None) {
		t->title = XCreateWindow(dpy, row->bar, t->x, 0, t->w, config.titlewidth, 0,
		                         CopyFromParent, CopyFromParent, CopyFromParent,
		                         CWEventMask, &clientswa);
	} else {
		XReparentWindow(dpy, t->title, row->bar, t->x, 0);
	}
	XReparentWindow(dpy, t->frame, row->frame, 0, 0);
	XMapWindow(dpy, t->frame);
	XMapWindow(dpy, t->title);
	if (oldrow != NULL) {           /* deal with the row this tab came from */
		if (oldrow->ntabs == 0) {
			rowdel(oldrow);
		} else {
			rowcalctabs(oldrow);
		}
	}
}

/* check if desktop is visible */
static int
deskisvisible(struct Desktop *desk)
{
	return desk->mon->seldesk == desk;
}

/* (un)show desktop */
static void
deskshow(int show)
{
	struct Container *c;

	for (c = wm.c; c != NULL; c = c->next)
		if (!c->isminimized)
			containerhide(c, show);
	wm.showingdesk = show;
	ewmhsetshowingdesktop(show);
}

/* change desktop */
static void
deskfocus(struct Desktop *desk, int focus)
{
	void tabfocus(struct Tab *t, int gotodesk);
	struct Container *c;

	if (desk == NULL || desk == wm.selmon->seldesk)
		return;
	if (!deskisvisible(desk)) {
		/* unhide cointainers of new current desktop
		 * hide containers of previous current desktop */
		for (c = wm.c; c != NULL; c = c->next) {
			if (!c->isminimized && c->desk == desk) {
				containerhide(c, 0);
			} else if (!c->issticky && c->desk == desk->mon->seldesk) {
				containerhide(c, 1);
			}
		}
	}

	/* update current desktop */
	wm.selmon = desk->mon;
	wm.selmon->seldesk = desk;
	if (wm.showingdesk)
		deskshow(0);
	ewmhsetcurrentdesktop(desk->n);

	/* focus client on the new current desktop */
	if (focus) {
		c = getnextfocused(desk->mon, desk);
		if (c != NULL) {
			tabfocus(c->selcol->selrow->seltab, 0);
		} else {
			tabfocus(NULL, 0);
		}
	}
}

/* move container x pixels to the right and y pixels down */
static void
containerincrmove(struct Container *c, int x, int y)
{
	struct Monitor *monto;
	struct Container *tmp;

	if (c == NULL || c->isminimized || c->ismaximized || c->isfullscreen)
		return;
	c->nx += x;
	c->ny += y;
	c->x = c->nx;
	c->y = c->ny;
	if (config.snap > 0) {
		if (abs(c->y - c->mon->wy) < config.snap) {
			c->y = c->mon->wy;
		}
		if (abs(c->y + c->h - c->mon->wy - c->mon->wh) < config.snap) {
			c->y = c->mon->wy + c->mon->wh - c->h;
		}
		if (abs(c->x - c->mon->wx) < config.snap) {
			c->x = c->mon->wx;
		}
		if (abs(c->x + c->w - c->mon->wx - c->mon->ww) < config.snap) {
			c->x = c->mon->wx + c->mon->ww - c->w;
		}
		for (tmp = wm.c; tmp != NULL; tmp = tmp->next) {
			if (!tmp->isminimized && tmp->mon == c->mon &&
			    (tmp->issticky || tmp->desk == c->desk)) {
				if (c->x + c->w >= tmp->x && c->x <= tmp->x + tmp->w) {
					if (abs(c->y + c->h - tmp->y) < config.snap) {
						c->y = tmp->y - c->h;
					}
					if (abs(c->y - tmp->y) < config.snap) {
						c->y = tmp->y;
					}
					if (abs(c->y + c->h - tmp->y - tmp->h) < config.snap) {
						c->y = tmp->y + tmp->h - c->h;
					}
					if (abs(c->y - tmp->y - tmp->h) < config.snap) {
						c->y = tmp->y + tmp->h;
					}
				}
				if (c->y + c->h >= tmp->y && c->y <= tmp->y + tmp->h) {
					if (abs(c->x + c->w - tmp->x) < config.snap) {
						c->x = tmp->x - c->w;
					}
					if (abs(c->x - tmp->x) < config.snap) {
						c->x = tmp->x;
					}
					if (abs(c->x + c->w - tmp->x - tmp->w) < config.snap) {
						c->x = tmp->x + tmp->w - c->w;
					}
					if (abs(c->x - tmp->x - tmp->w) < config.snap) {
						c->x = tmp->x + tmp->w;
					}
				}
			}
		}
	}
	containermoveresize(c);
	if (!c->issticky) {
		monto = getmon(c->nx + c->nw / 2, c->ny + c->nh / 2);
		if (monto != NULL && monto != c->mon) {
			containersendtodesk(c, monto->seldesk, 0, 0);
			if (wm.focused == c) {
				deskfocus(monto->seldesk, 0);
			}
		}
	}
}

/* create new tab */
static struct Tab *
tabnew(Window win, int ignoreunmap)
{
	struct Tab *t;

	t = emalloc(sizeof(*t));
	t->prev = t->next = NULL;
	t->row = NULL;
	t->ds = NULL;
	t->name = NULL;
	t->ignoreunmap = ignoreunmap;
	t->isurgent = 0;
	t->winw = t->winh = 0;
	t->x = t->w = 0;
	t->pw = 0;
	t->pix = None;
	t->pixtitle = None;
	t->title = None;
	t->win = win;
	t->frame = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                         CopyFromParent, CopyFromParent, CopyFromParent,
	                         CWEventMask, &clientswa);
	XReparentWindow(dpy, t->win, t->frame, 0, 0);
	XMapWindow(dpy, t->win);
	icccmwmstate(win, NormalState);
	clientsincr();
	return t;
}

/* clear window urgency */
static void
tabclearurgency(struct Tab *t)
{
	XWMHints wmh = {0};

	XSetWMHints(dpy, t->win, &wmh);
	t->isurgent = 0;
}

/* update tab urgency */
static void
tabupdateurgency(struct Tab *t, int isurgent)
{
	int prev;

	prev = t->isurgent;
	t->isurgent = isurgent;
	if (t->isurgent && t->row->col->c == wm.focused && t == t->row->seltab) {
		tabclearurgency(t);
	}
	if (prev != t->isurgent) {
		tabdecorate(t, 0);
	}
}

/* focus tab */
void
tabfocus(struct Tab *t, int gotodesk)
{
	struct Container *c;

	wm.prevfocused = wm.focused;
	if (t == NULL) {
		wm.focused = NULL;
		XSetInputFocus(dpy, wm.focuswin, RevertToParent, CurrentTime);
		ewmhsetactivewindow(None);
	} else {
		c = t->row->col->c;
		if (!c->isfullscreen && getfullscreen(c->mon, c->desk) != NULL)
			return;         /* we should not focus a client below a fullscreen client */
		wm.focused = c;
		t->row->seltab = t;
		t->row->col->selrow = t->row;
		t->row->col->c->selcol = t->row->col;
		if (t->row->col->maxrow != NULL && t->row->col->maxrow != t->row)
			rowstack(t->row->col, t->row);
		XRaiseWindow(dpy, t->frame);
		if (c->isshaded) {
			XSetInputFocus(dpy, c->frame, RevertToParent, CurrentTime);
			ewmhsetactivewindow(t->win);
		} else if (t->ds != NULL) {
			XRaiseWindow(dpy, t->ds->frame);
			XSetInputFocus(dpy, t->ds->win, RevertToParent, CurrentTime);
			ewmhsetactivewindow(t->ds->win);
		} else {
			XSetInputFocus(dpy, t->win, RevertToParent, CurrentTime);
			ewmhsetactivewindow(t->win);
		}
		if (t->isurgent)
			tabclearurgency(t);
		containeraddfocus(c);
		containerdecorate(c, NULL, NULL, 1, 0);
		containerminimize(c, 0, 0);
		containerraise(c);
		if (gotodesk) {
			deskfocus(c->issticky ? c->mon->seldesk : c->desk, 0);
		}
		shodgrouptab(c);
		shodgroupcontainer(c);
		ewmhsetstate(c);
	}
	if (wm.prevfocused != NULL) {
		containerdecorate(wm.prevfocused, NULL, NULL, 1, 0);
		ewmhsetstate(wm.prevfocused);
	}
}

/* update tab title */
static void
tabupdatetitle(struct Tab *t)
{
	free(t->name);
	t->name = getwinname(t->win);
}

/* try to attach tab in a client of specified client list */
static int
tryattach(struct Container *list, struct Tab *det, int xroot, int yroot)
{
	struct Container *c;
	struct Column *col, *ncol;
	struct Row *row, *nrow;
	struct Tab *t, *next;
	int rowy, rowh;

	for (c = list; c != NULL; c = c->rnext) {
		if (c->ishidden || xroot < c->x || xroot >= c->x + c->w || yroot < c->y || yroot >= c->y + c->h)
			continue;
		for (col = c->cols; col != NULL; col = col->next) {
			if (xroot - c->x >= col->x - DROPPIXELS &&
				   xroot - c->x < col->x + col->w + DROPPIXELS) {
				if (yroot - c->y < c->b) {
					nrow = rownew();
					coladdrow(col, nrow, NULL);
					rowaddtab(nrow, det, NULL);
					colcalcrows(col, 1, 1);
					goto done;
				}
				rowy = c->b;
				for (row = col->rows; row != NULL; row = row->next) {
					if (col->maxrow != NULL) {
						if (row == col->maxrow) {
							rowh = c->h - 2 * c->b - (col->nrows - 1) * config.titlewidth;
						} else {
							rowh = config.titlewidth;
						}
					} else {
						rowh = row->h;
					}
					if (yroot - c->y >= rowy &&
					    yroot - c->y < rowy + config.titlewidth) {
						for (next = t = row->tabs; t != NULL; t = t->next) {
							next = t;
							if (xroot - c->x + col->x < col->x + t->x + t->w / 2) {
								rowaddtab(row, det, t->prev);
								rowcalctabs(row);
								goto done;
							}
						}
						if (next != NULL) {
							rowaddtab(row, det, next);
							rowcalctabs(row);
							goto done;
						}
					}
					if (yroot - c->y >= rowy + rowh - DROPPIXELS &&
					    yroot - c->y < rowy + rowh + config.divwidth) {
						nrow = rownew();
						coladdrow(col, nrow, row);
						rowaddtab(nrow, det, NULL);
						colcalcrows(col, 1, 1);
						goto done;
					}
					rowy += rowh + config.divwidth;
				}
			}
			if (xroot - c->x >= col->x + col->w - DROPPIXELS &&
			    xroot - c->x < col->x + col->w + config.divwidth + DROPPIXELS) {
				nrow = rownew();
				ncol = colnew();
				containeraddcol(c, ncol, col);
				coladdrow(ncol, nrow, NULL);
				rowaddtab(nrow, det, NULL);
				containercalccols(c, 1, 1);
				goto done;
			}
		}
		if (xroot - c->x < c->b + DROPPIXELS) {
			nrow = rownew();
			ncol = colnew();
			containeraddcol(c, ncol, NULL);
			coladdrow(ncol, nrow, NULL);
			rowaddtab(nrow, det, NULL);
			containercalccols(c, 1, 1);
			goto done;
		}
		break;
	}
	return 0;
done:
	tabfocus(det, 0);
	XMapSubwindows(dpy, c->frame);
	/* no need to call shodgrouptab() and shodgroupcontainer(); tabfocus() already calls them */
	ewmhsetclientsstacking();
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
	return 1;
}

/* attach tab into row; return 1 if succeeded, zero otherwise */
static int
tabattach(struct Tab *t, int xroot, int yroot)
{
	if (tryattach(wm.fulllist, t, xroot, yroot))
		return 1;
	if (tryattach(wm.abovelist, t, xroot, yroot))
		return 1;
	if (tryattach(wm.centerlist, t, xroot, yroot))
		return 1;
	if (tryattach(wm.belowlist, t, xroot, yroot))
		return 1;
	return 0;
}

/* create new dialog */
static struct Dialog *
dialognew(Window win, int maxw, int maxh, int ignoreunmap)
{
	struct Dialog *d;

	d = emalloc(sizeof(*d));
	d->prev = d->next = NULL;
	d->t = NULL;
	d->x = d->y = d->w = d->h = 0;
	d->pix = None;
	d->pw = d->ph = 0;
	d->maxw = maxw;
	d->maxh = maxh;
	d->ignoreunmap = ignoreunmap;
	d->win = win;
	d->frame = XCreateWindow(dpy, root, 0, 0, maxw, maxh, 0,
	                         CopyFromParent, CopyFromParent, CopyFromParent,
	                         CWEventMask, &clientswa);
	XReparentWindow(dpy, d->win, d->frame, 0, 0);
	XMapWindow(dpy, d->win);
	clientsincr();
	return d;
}

/* check if monitor geometry is unique */
static int
monisuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}

/* add monitor */
static void
monnew(XineramaScreenInfo *info)
{
	struct Monitor *mon;
	int i;

	mon = emalloc(sizeof *mon);
	mon->prev = NULL;
	mon->next = NULL;
	mon->mx = mon->wx = info->x_org;
	mon->my = mon->wy = info->y_org;
	mon->mw = mon->ww = info->width;
	mon->mh = mon->wh = info->height;
	mon->desks = ecalloc(config.ndesktops, sizeof(*mon->desks));
	for (i = 0; i < config.ndesktops; i++) {
		mon->desks[i].mon = mon;
		mon->desks[i].n = i;
	}
	mon->seldesk = &mon->desks[0];
	if (wm.montail != NULL) {
		wm.montail->next = mon;
		mon->prev = wm.montail;
	} else {
		wm.monhead = mon;
	}
	wm.montail = mon;
}

/* delete monitor and set monitor of clients on it to NULL */
static void
mondel(struct Monitor *mon)
{
	struct Container *c;

	if (mon->next)
		mon->next->prev = mon->prev;
	else
		wm.montail = mon->prev;
	if (mon->prev)
		mon->prev->next = mon->next;
	else
		wm.monhead = mon->next;
	for (c = wm.c; c; c = c->next)
		if (c->mon == mon)
			c->mon = NULL;
	free(mon->desks);
	free(mon);
}

/* update the list of monitors */
static void
monupdate(void)
{
	XineramaScreenInfo *info = NULL;
	XineramaScreenInfo *unique = NULL;
	struct Monitor *mon;
	struct Monitor *tmp;
	struct Container *c, *focus;
	int delselmon = 0;
	int del, add;
	int i, j, n;
	int moncount;

	info = XineramaQueryScreens(dpy, &n);
	unique = ecalloc(n, sizeof *unique);
	
	/* only consider unique geometries as separate screens */
	for (i = 0, j = 0; i < n; i++)
		if (monisuniquegeom(unique, j, &info[i]))
			memcpy(&unique[j++], &info[i], sizeof *unique);
	XFree(info);
	moncount = j;

	/* look for monitors that do not exist anymore and delete them */
	mon = wm.monhead;
	while (mon) {
		del = 1;
		for (i = 0; i < moncount; i++) {
			if (unique[i].x_org == mon->mx && unique[i].y_org == mon->my &&
			    unique[i].width == mon->mw && unique[i].height == mon->mh) {
				del = 0;
				break;
			}
		}
		tmp = mon;
		mon = mon->next;
		if (del) {
			if (tmp == wm.selmon)
				delselmon = 1;
			mondel(tmp);
		}
	}

	/* look for new monitors and add them */
	for (i = 0; i < moncount; i++) {
		add = 1;
		for (mon = wm.monhead; mon; mon = mon->next) {
			if (unique[i].x_org == mon->mx && unique[i].y_org == mon->my &&
			    unique[i].width == mon->mw && unique[i].height == mon->mh) {
				add = 0;
				break;
			}
		}
		if (add) {
			monnew(&unique[i]);
		}
	}
	if (delselmon)
		wm.selmon = wm.monhead;

	/* update monitor number */
	for (i = 0, mon = wm.monhead; mon; mon = mon->next, i++)
		mon->n = i;

	/* send containers which do not belong to a window to selected desktop */
	focus = NULL;
	for (c = wm.c; c; c = c->next) {
		if (!c->isminimized && c->mon == NULL) {
			focus = c;
			containersendtodesk(c, wm.selmon->seldesk, 1, 0);
			containermoveresize(c);
		}
	}
	if (focus != NULL)              /* if a contained changed desktop, focus it */
		tabfocus(focus->selcol->selrow->seltab, 1);

	free(unique);
}

/* update window area and dock area of monitor */
static void
monupdatearea(void)
{
	struct Monitor *mon;
	struct Bar *bar;
	struct Container *c;
	int t, b, l, r;

	for (mon = wm.monhead; mon != NULL; mon = mon->next) {
		mon->wx = mon->mx;
		mon->wy = mon->my;
		mon->ww = mon->mw;
		mon->wh = mon->mh;
		t = b = l = r = 0;
		if (mon == wm.monhead && dock.mapped) {
			switch (config.dockgravity[0]) {
			case 'N':
				t = config.dockwidth;
				break;
			case 'S':
				b = config.dockwidth;
				break;
			case 'W':
				l = config.dockwidth;
				break;
			case 'E':
			default:
				r = config.dockwidth;
				break;
			}
		}
		for (bar = wm.bars; bar != NULL; bar = bar->next) {
			if (bar->strut[STRUT_TOP] != 0) {
				if (bar->strut[STRUT_TOP] >= mon->my &&
				    bar->strut[STRUT_TOP] < mon->my + mon->mh &&
				    (!bar->partial ||
				     (bar->strut[STRUT_TOP_START_X] >= mon->mx &&
				     bar->strut[STRUT_TOP_END_X] <= mon->mx + mon->mw))) {
					t = max(t, bar->strut[STRUT_TOP] - mon->my);
				}
			} else if (bar->strut[STRUT_BOTTOM] != 0) {
				if (screenh - bar->strut[STRUT_BOTTOM] <= mon->my + mon->mh &&
				    screenh - bar->strut[STRUT_BOTTOM] > mon->my &&
				    (!bar->partial ||
				     (bar->strut[STRUT_BOTTOM_START_X] >= mon->mx &&
				     bar->strut[STRUT_BOTTOM_END_X] <= mon->mx + mon->mw))) {
					b = max(b, bar->strut[STRUT_BOTTOM] - (screenh - (mon->my + mon->mh)));
				}
			} else if (bar->strut[STRUT_LEFT] != 0) {
				if (bar->strut[STRUT_LEFT] >= mon->mx &&
				    bar->strut[STRUT_LEFT] < mon->mx + mon->mw &&
				    (!bar->partial ||
				     (bar->strut[STRUT_LEFT_START_Y] >= mon->my &&
				     bar->strut[STRUT_LEFT_END_Y] <= mon->my + mon->mh))) {
					l = max(l, bar->strut[STRUT_LEFT] - mon->mx);
				}
			} else if (bar->strut[STRUT_RIGHT] != 0) {
				if (screenw - bar->strut[STRUT_RIGHT] <= mon->mx + mon->mw &&
				    screenw - bar->strut[STRUT_RIGHT] > mon->mx &&
				    (!bar->partial ||
				     (bar->strut[STRUT_RIGHT_START_Y] >= mon->my &&
				     bar->strut[STRUT_RIGHT_END_Y] <= mon->my + mon->mh))) {
					r = max(r, bar->strut[STRUT_RIGHT] - (screenw - (mon->mx + mon->mw)));
				}
			}
		}
		mon->wy += t;
		mon->wh -= t + b;
		mon->wx += l;
		mon->ww -= l + r;
	}
	for (c = wm.c; c != NULL; c = c->next) {
		if (c->ismaximized) {
			containercalccols(c, 0, 1);
			containermoveresize(c);
			containerredecorate(c, NULL, NULL, 0);
		}
	}
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

/* check if event is related to the prompt or its frame */
static Bool
promptvalidevent(Display *dpy, XEvent *ev, XPointer arg)
{
	struct Prompt *prompt;

	(void)dpy;
	prompt = (struct Prompt *)arg;
	switch(ev->type) {
	case DestroyNotify:
		if (ev->xdestroywindow.window == prompt->win)
			return True;
		break;
	case UnmapNotify:
		if (ev->xunmap.window == prompt->win)
			return True;
		break;
	case ConfigureRequest:
		if (ev->xconfigurerequest.window == prompt->win)
			return True;
		break;
	case Expose:
	case ButtonPress:
		return True;
	}
	return False;
}

/* calculate position and size of prompt window and the size of its frame */
static void
promptcalcgeom(int *x, int *y, int *w, int *h, int *fw, int *fh)
{
	*w = min(*w, wm.selmon->ww - config.borderwidth * 2);
	*h = min(*h, wm.selmon->wh - config.borderwidth);
	*x = wm.selmon->wx + (wm.selmon->ww - *w) / 2 - config.borderwidth;
	*y = 0;
	*fw = *w + config.borderwidth * 2;
	*fh = *h + config.borderwidth;
}

/* create notification window */
static void
notifnew(Window win, int w, int h)
{
	struct Notification *n;

	n = emalloc(sizeof(*n));
	n->w = w + 2 * config.borderwidth;
	n->h = h + 2 * config.borderwidth;
	n->pw = n->ph = 0;
	n->prev = wm.ntail;
	n->next = NULL;
	if (wm.ntail != NULL)
		wm.ntail->next = n;
	else
		wm.nhead = n;
	wm.ntail = n;
	n->pix = None;
	n->win = win;
	n->frame = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
	                             CopyFromParent, CopyFromParent, CopyFromParent,
	                             CWEventMask,
	                             &(XSetWindowAttributes){.event_mask = SubstructureNotifyMask | SubstructureRedirectMask});
	XReparentWindow(dpy, n->win, n->frame, 0, 0);
	XMapWindow(dpy, n->win);
}

/* place notifications */
static void
notifplace(void)
{
	struct Notification *n;
	int x, y, h;

	h = 0;
	for (n = wm.nhead; n; n = n->next) {
		x = wm.monhead->wx;
		y = wm.monhead->wy;
		switch (config.notifgravity[0]) {
		case 'N':
			switch (config.notifgravity[1]) {
			case 'W':
				break;
			case 'E':
				x += wm.monhead->ww - n->w;
				break;
			default:
				x += (wm.monhead->ww - n->w) / 2;
				break;
			}
			break;
		case 'S':
			switch(config.notifgravity[1]) {
			case 'W':
				y += wm.monhead->wh - n->h;
				break;
			case 'E':
				x += wm.monhead->ww - n->w;
				y += wm.monhead->wh - n->h;
				break;
			default:
				x += (wm.monhead->ww - n->w) / 2;
				y += wm.monhead->wh - n->h;
				break;
			}
			break;
		case 'W':
			y += (wm.monhead->wh - n->h) / 2;
			break;
		case 'C':
			x += (wm.monhead->ww - n->w) / 2;
			y += (wm.monhead->wh - n->h) / 2;
			break;
		case 'E':
			x += wm.monhead->ww - n->w;
			y += (wm.monhead->wh - n->h) / 2;
			break;
		default:
			x += wm.monhead->ww - n->w;
			break;
		}

		if (config.notifgravity[0] == 'S')
			y -= h;
		else
			y += h;
		h += n->h + config.notifgap + config.borderwidth * 2;

		XMoveResizeWindow(dpy, n->frame, x, y, n->w, n->h);
		XMoveResizeWindow(dpy, n->win, config.borderwidth, config.borderwidth, n->w - 2 * config.borderwidth, n->h - 2 * config.borderwidth);
		XMapWindow(dpy, n->frame);
		winnotify(n->win, x + config.borderwidth, y + config.borderwidth, n->w - 2 * config.borderwidth, n->h - 2 * config.borderwidth);
		if (n->pw != n->w || n->ph != n->h) {
			notifdecorate(n);
		}
	}
}

/* delete notification */
static void
notifdel(struct Notification *n)
{
	if (n->next)
		n->next->prev = n->prev;
	else
		wm.ntail = n->prev;
	if (n->prev)
		n->prev->next = n->next;
	else
		wm.nhead = n->next;
	if (n->pix != None)
		XFreePixmap(dpy, n->pix);
	XReparentWindow(dpy, n->win, root, 0, 0);
	XDestroyWindow(dpy, n->frame);
	free(n);
	notifplace();
}

/* fill strut array of bar */
static void
barstrut(struct Bar *bar)
{
	unsigned long *arr;
	unsigned long l, i;

	for (i = 0; i < STRUT_LAST; i++)
		bar->strut[i] = 0;
	bar->partial = 1;
	l = getcardprop(bar->win, atoms[_NET_WM_STRUT_PARTIAL], &arr);
	if (arr == NULL) {
		bar->partial = 0;
		l = getcardprop(bar->win, atoms[_NET_WM_STRUT], &arr);
		if (arr == NULL) {
			return;
		}
	}
	for (i = 0; i < STRUT_LAST && i < l; i++)
		bar->strut[i] = arr[i];
	XFree(arr);
}

/* delete bar */
static void
bardel(struct Bar *bar)
{
	if (bar->next != NULL)
		bar->next->prev = bar->prev;
	if (bar->prev != NULL)
		bar->prev->next = bar->next;
	else
		wm.bars = bar->next;
	free(bar);
	monupdatearea();
}

/* decorate dock */
static void
dockdecorate(void)
{
	XGCValues val;

	if (dock.pw != dock.w || dock.ph != dock.h || dock.pix == None) {
		if (dock.pix != None)
			XFreePixmap(dpy, dock.pix);
		dock.pix = XCreatePixmap(dpy, dock.win, dock.w, dock.h, depth);
	}
	dock.pw = dock.w;
	dock.ph = dock.h;
	val.fill_style = FillSolid;
	val.foreground = visual.dock[COLOR_MID];
	XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
	XFillRectangle(dpy, dock.pix, gc, 0, 0, dock.w, dock.h);

	drawrectangle(dock.pix, 1, 0, 0, dock.w, dock.h, visual.dock[COLOR_LIGHT], visual.dock[COLOR_DARK]);

	XCopyArea(dpy, dock.pix, dock.win, gc, 0, 0, dock.w, dock.h, 0, 0);
}

/* update dock position; create it, if necessary */
static void
dockupdate(void)
{
	struct Dockapp *dapp;
	Window wins[2];
	int size;

	size = 0;
	for (dapp = dock.head; dapp != NULL; dapp = dapp->next) {
		switch (config.dockgravity[0]) {
		case 'N':
			dapp->x = 1 + size + dapp->gw / 2 - dapp->w / 2;
			dapp->y = config.dockspace / 2 - dapp->h / 2;
			size += dapp->gw;
			break;
		case 'S':
			dapp->x = 1 + size + dapp->gw / 2 - dapp->w / 2;
			dapp->y = 1 + config.dockspace / 2 - dapp->h / 2;
			size += dapp->gw;
			break;
		case 'W':
			dapp->x = config.dockspace / 2 - dapp->w / 2;
			dapp->y = 1 + size + dapp->gh / 2 - dapp->h / 2;
			size += dapp->gh;
			break;
		case 'E':
		default:
			dapp->x = 1 + config.dockspace / 2 - dapp->w / 2;
			dapp->y = 1 + size + dapp->gh / 2 - dapp->h / 2;
			size += dapp->gh;
			break;
		}
	}
	if (size == 0) {
		XUnmapWindow(dpy, dock.win);
		dock.mapped = 0;
		return;
	}
	dock.mapped = 1;
	size += 2;
	switch (config.dockgravity[0]) {
	case 'N':
		dock.h = config.dockwidth;
		dock.y = 0;
		break;
	case 'S':
		dock.h = config.dockwidth;
		dock.y = wm.monhead->mh - config.dockwidth;
		break;
	case 'W':
		dock.w = config.dockwidth;
		dock.x = 0;
		break;
	case 'E':
	default:
		dock.w = config.dockwidth;
		dock.x = wm.monhead->mw - config.dockwidth;
		dock.h = min(size, wm.monhead->mh);
		dock.y = wm.monhead->mh / 2 - size / 2;
		break;
	}
	if (config.dockgravity[0] == 'N' || config.dockgravity[0] == 'S') {
		switch (config.dockgravity[1]) {
		case 'W':
			dock.w = min(size, wm.monhead->mw);
			dock.x = 0;
			break;
		case 'E':
			dock.w = min(size, wm.monhead->mw);
			dock.x = wm.monhead->mw - size;
			break;
		default:
			dock.w = min(size, wm.monhead->mw);
			dock.x = wm.monhead->mw / 2 - size / 2;
			break;
		}
	} else if (config.dockgravity[0] != '\0') {
		switch (config.dockgravity[1]) {
		case 'N':
			dock.h = min(size, wm.monhead->mh);
			dock.y = 0;
			break;
		case 'S':
			dock.h = min(size, wm.monhead->mh);
			dock.y = wm.monhead->mh - size;
			break;
		default:
			dock.h = min(size, wm.monhead->mh);
			dock.y = wm.monhead->mh / 2 - size / 2;
			break;
		}
	}
	for (dapp = dock.head; dapp != NULL; dapp = dapp->next) {
		XMoveWindow(dpy, dapp->win, dapp->x, dapp->y);
	}
	dockdecorate();
	wins[0] = wm.layerwins[LAYER_DOCK];
	wins[1] = dock.win;
	XMoveResizeWindow(dpy, dock.win, dock.x, dock.y, dock.w, dock.h);
	XRestackWindows(dpy, wins, 2);
	XMapWindow(dpy, dock.win);
	XMapSubwindows(dpy, dock.win);
}

/* create dockapp */
static void
dockappnew(Window win, int w, int h, int ignoreunmap)
{
	struct Dockapp *dapp;

	dapp = emalloc(sizeof(*dapp));
	dapp->win = win;
	dapp->x = dapp->y = 0;
	dapp->w = w;
	dapp->h = h;
	dapp->gw = config.dockspace * (((w - 1) / config.dockspace) + 1);
	dapp->gh = config.dockspace * (((h - 1) / config.dockspace) + 1);
	dapp->prev = dock.tail;
	dapp->next = NULL;
	dapp->ignoreunmap = ignoreunmap;
	if (dock.tail != NULL)
		dock.tail->next = dapp;
	else
		dock.head = dapp;
	dock.tail = dapp;
}

/* delete dockapp */
static void
dockappdel(struct Dockapp *dapp)
{
	if (dapp->next != NULL)
		dapp->next->prev = dapp->prev;
	else
		dock.tail = dapp->prev;
	if (dapp->prev != NULL)
		dapp->prev->next = dapp->next;
	else
		dock.head = dapp->next;
	XReparentWindow(dpy, dapp->win, root, 0, 0);
	free(dapp);
	dockupdate();
	monupdatearea();
}

/* call the proper decorate function */
static void
decorate(struct Winres *res)
{
	int fullw, fullh;

	if (res->dock) {
		XCopyArea(dpy, res->dock->pix, res->dock->win, gc, 0, 0, res->dock->w, res->dock->h, 0, 0);
	} else if (res->n) {
		XCopyArea(dpy, res->n->pix, res->n->frame, gc, 0, 0, res->n->w, res->n->h, 0, 0);
	} else if (res->d != NULL) {
		fullw = res->d->w + 2 * config.borderwidth;
		fullh = res->d->h + 2 * config.borderwidth;
		XCopyArea(dpy, res->d->pix, res->d->frame, gc, 0, 0, fullw, fullh, 0, 0);
	} else if (res->t != NULL) {
		XCopyArea(dpy, res->t->pixtitle, res->t->title, gc, 0, 0, res->t->w, config.titlewidth, 0, 0);
		XCopyArea(dpy, res->t->pix, res->t->frame, gc, 0, 0, res->t->winw, res->t->winh, 0, 0);
	} else if (res->row != NULL) {
		XCopyArea(dpy, res->row->pixbar, res->row->bar, gc, 0, 0, res->row->pw, config.titlewidth, 0, 0);
		XCopyArea(dpy, res->row->pixbl, res->row->bl, gc, 0, 0, config.titlewidth, config.titlewidth, 0, 0);
		XCopyArea(dpy, res->row->pixbr, res->row->br, gc, 0, 0, config.titlewidth, config.titlewidth, 0, 0);
	} else if (res->c != NULL) {
		fullw = res->c->w;
		fullh = res->c->h;
		XCopyArea(dpy, res->c->pix, res->c->frame, gc, 0, 0, fullw, fullh, 0, 0);
	}
}

/* add dialog window into tab */
static void
managedialog(struct Tab *t, struct Dialog *d)
{
	d->t = t;
	XReparentWindow(dpy, d->frame, t->frame, 0, 0);
	if (t->ds)
		t->ds->prev = d;
	d->next = t->ds;
	t->ds = d;
	icccmwmstate(d->win, NormalState);
	dialogcalcsize(d);
	dialogmoveresize(d);
	XMapRaised(dpy, d->frame);
	if (wm.focused != NULL && wm.focused->selcol->selrow->seltab == t)
		tabfocus(t, 0);
	ewmhsetclients();
	ewmhsetclientsstacking();
}

/* map prompt, give it focus, wait for it to close, then revert focus to previously focused window */
static void
manageprompt(Window win, int w, int h)
{
	struct Prompt prompt;
	struct Winres res;
	XEvent ev;
	int x, y, fw, fh;

	promptcalcgeom(&x, &y, &w, &h, &fw, &fh);
	prompt.frame = XCreateWindow(dpy, root, x, y, fw, fh, 0,
	                             CopyFromParent, CopyFromParent, CopyFromParent,
	                             CWEventMask, &clientswa);
	prompt.pix = None;
	prompt.ph = prompt.pw = 0;
	XReparentWindow(dpy, win, prompt.frame, config.borderwidth, 0);
	XMapWindow(dpy, win);
	XMapWindow(dpy, prompt.frame);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	prompt.win = win;
	while (!XIfEvent(dpy, &ev, promptvalidevent, (XPointer)&prompt)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				if (ev.xexpose.window == prompt.frame) {
					promptdecorate(&prompt, fw, fh);
				} else {
					res = getwin(ev.xexpose.window);
					decorate(&res);
				}
			}
			break;
		case DestroyNotify:
		case UnmapNotify:
			goto done;
			break;
		case ConfigureRequest:
			w = ev.xconfigurerequest.width;
			h = ev.xconfigurerequest.height;
			promptcalcgeom(&x, &y, &w, &h, &fw, &fh);
			XMoveResizeWindow(dpy, prompt.frame, x, y, fw, fh);
			XMoveResizeWindow(dpy, win, config.borderwidth, 0, w, h);
			break;
		case ButtonPress:
			if (ev.xbutton.window != win && ev.xbutton.window != prompt.frame)
				winclose(win);
			XAllowEvents(dpy, ReplayPointer, CurrentTime);
			break;
		}
	}
done:
	XReparentWindow(dpy, win, root, 0, 0);
	XDestroyWindow(dpy, prompt.frame);
	if (wm.focused) {
		tabfocus(wm.focused->selcol->selrow->seltab, 0);
	} else {
		tabfocus(NULL, 0);
	}
}

/* map desktop window */
static void
managedesktop(Window win)
{
	Window wins[2] = {wm.layerwins[LAYER_DESKTOP], win};

	XRestackWindows(dpy, wins, 2);
	XMapWindow(dpy, win);
}

/* map dockapp window */
static void
managedockapp(Window win, int w, int h, int ignoreunmap)
{
	XReparentWindow(dpy, win, dock.win, 0, 0);
	dockappnew(win, w, h, ignoreunmap);
	dockupdate();
	monupdatearea();
}

/* add notification window into notification queue; and update notification placement */
static void
managenotif(Window win, int w, int h)
{
	notifnew(win, w, h);
	notifplace();
}

/* create container for tab */
static void
managecontainer(struct Container *c, struct Tab *t, struct Desktop *desk, int userplaced)
{
	struct Column *col;
	struct Row *row;

	row = rownew();
	col = colnew();
	containeraddcol(c, col, NULL);
	coladdrow(col, row, NULL);
	rowaddtab(row, t, NULL);
	containersendtodesk(c, desk, 1, userplaced);
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
	XMapSubwindows(dpy, c->frame);
	containerhide(c, 0);
	tabfocus(t, 0);
	/* no need to call shodgrouptab() and shodgroupcontainer(); tabfocus() already calls them */
	ewmhsetclients();
	ewmhsetclientsstacking();
}

/* map bar window */
static void
managebar(Window win)
{
	struct Bar *bar;
	Window wins[2] = {wm.layerwins[LAYER_DOCK], win};

	bar = emalloc(sizeof(*bar));
	bar->prev = bar->next = NULL;
	bar->win = win;
	if (wm.bars != NULL)
		wm.bars->prev = bar;
	bar->next = wm.bars;
	wm.bars = bar;
	XRestackWindows(dpy, wins, 2);
	XMapWindow(dpy, win);
	barstrut(bar);
	monupdatearea();
}

/* call one of the manage- functions */
static void
manage(Window win, XWindowAttributes *wa, int ignoreunmap)
{
	struct Winres res;
	struct Tab *t;
	struct Container *c;
	struct Dialog *d;
	int userplaced;

	res = getwin(win);
	if (res.dock != NULL || res.c != NULL || res.bar != NULL || res.dapp != NULL || res.n != NULL)
		return;
	switch (getwintype(win)) {
	case TYPE_DESKTOP:
		managedesktop(win);
		break;
	case TYPE_DOCKAPP:
		managedockapp(win, wa->width, wa->height, ignoreunmap);
		break;
	case TYPE_DOCK:
		managebar(win);
		break;
	case TYPE_NOTIFICATION:
		preparewin(win);
		managenotif(win, wa->width, wa->height);
		break;
	case TYPE_PROMPT:
		preparewin(win);
		manageprompt(win, wa->width, wa->height);
		break;
	default:
		preparewin(win);
		if ((t = getdialogfor(win)) != NULL) {
			d = dialognew(win, wa->width, wa->height, ignoreunmap);
			managedialog(t, d);
		} else {
			userplaced = isuserplaced(win);
			t = tabnew(win, ignoreunmap);
			tabupdatetitle(t);
			c = containernew(wa->x, wa->y, wa->width, wa->height);
			managecontainer(c, t, wm.selmon->seldesk, userplaced);
		}
		break;
	}
}

/* unmanage tab (and delete its row if it is the only tab) */
static void
unmanage(struct Tab *t)
{
	struct Container *c, *next;
	struct Column *col;
	struct Row *row;
	struct Desktop *desk;
	int moveresize;
	int focus;

	row = t->row;
	col = row->col;
	c = col->c;
	desk = c->desk;
	moveresize = 1;
	next = c;
	tabdel(t);
	focus = (c == wm.focused);
	if (row->ntabs == 0) {
		rowdel(row);
		if (col->nrows == 0) {
			coldel(col);
			if (c->ncols == 0) {
				containerdel(c);
				next = getnextfocused(desk->mon, desk);
				moveresize = 0;
			}
		}
	}
	if (moveresize) {
		containercalccols(c, 1, 1);
		containermoveresize(c);
		containerredecorate(c, NULL, NULL, 0);
		shodgrouptab(c);
		shodgroupcontainer(c);
	}
	if (focus) {
		tabfocus((next != NULL) ? next->selcol->selrow->seltab : NULL, 0);
	}
}

/* scan for already existing windows and adopt them */
static void
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
				manage(wins[i], &wa, IGNOREUNMAP);
			}
		}
		for (i = 0; i < num; i++) {     /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &transwin) &&
			   (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)) {
				manage(wins[i], &wa, IGNOREUNMAP);
			}
		}
		if (wins != NULL) {
			XFree(wins);
		}
	}
}

/* map and hide focus window */
static void
mapfocuswin(void)
{
	XMoveWindow(dpy, wm.focuswin, -1, 0);
	XMapWindow(dpy, wm.focuswin);
}

/* detach tab from window with mouse */
static void
mouseretab(struct Tab *t, int xroot, int yroot, int x, int y)
{
	struct Monitor *mon;
	struct Container *c, *newc;
	struct Column *col;
	struct Row *row;
	struct Winres res;
	XEvent ev;
	int recalc, redraw;

	row = t->row;
	col = row->col;
	c = col->c;
	tabdetach(t, xroot - x, yroot - y, c->nw - 2 * config.borderwidth, c->nh - 2 * config.borderwidth - config.titlewidth);
	containermoveresize(c);
	XGrabPointer(dpy, t->title, False,
	             ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ExposureMask, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				if (ev.xexpose.window == t->title) {
					XCopyArea(dpy, t->pixtitle, t->title, gc, 0, 0, t->w, config.titlewidth, 0, 0);
				} else {
					res = getwin(ev.xexpose.window);
					decorate(&res);
				}
			}
			break;
		case MotionNotify:
			XMoveWindow(dpy, t->title, ev.xmotion.x_root - x, ev.xmotion.y_root - y);
			break;
		case ButtonRelease:
			xroot = ev.xbutton.x_root;
			yroot = ev.xbutton.y_root;
			XUnmapWindow(dpy, t->title);
			goto done;
			break;
		}
	}
done:
	XUngrabPointer(dpy, CurrentTime);
	if (!tabattach(t, xroot, yroot)) {
		mon = getmon(xroot - x, yroot - y);
		if (mon == NULL)
			mon = wm.selmon;
		newc = containernew(xroot - x - config.titlewidth, yroot - y, t->winw, t->winh);
		managecontainer(newc, t, mon->seldesk, 1);
	}
	recalc = 1;
	redraw = 0;
	if (row->ntabs == 0) {
		rowdel(row);
		redraw = 1;
	}
	if (col->nrows == 0) {
		coldel(col);
		redraw = 1;
	}
	if (c->ncols == 0) {
		containerdel(c);
		recalc = 0;
	}
	if (recalc) {
		containercalccols(c, 1, 1);
		containermoveresize(c);
		shodgrouptab(c);
		shodgroupcontainer(c);
		if (redraw) {
			containerdecorate(c, NULL, NULL, 0, 0);
		}
	}
}

/* resize container with mouse */
static void
mouseresize(struct Container *c, int xroot, int yroot, enum Octant o)
{
	struct Winres res;
	Cursor curs;
	XEvent ev;
	Time lasttime;
	int x, y, dx, dy;

	if (containerisshaded(c)) {
		if (o & W) {
			o = W;
		} else if (o & E) {
			o = E;
		}
	}

	switch (o) {
	case NW:
		curs = visual.cursors[CURSOR_NW];
		break;
	case NE:
		curs = visual.cursors[CURSOR_NE];
		break;
	case SW:
		curs = visual.cursors[CURSOR_SW];
		break;
	case SE:
		curs = visual.cursors[CURSOR_SE];
		break;
	case N:
		curs = visual.cursors[CURSOR_N];
		break;
	case S:
		curs = visual.cursors[CURSOR_S];
		break;
	case W:
		curs = visual.cursors[CURSOR_W];
		break;
	case E:
		curs = visual.cursors[CURSOR_E];
		break;
	default:
		curs = None;
		break;
	}
	if (o & W)
		x = xroot - c->x - c->b;
	else if (o & E)
		x = c->x + c->w - c->b - xroot;
	else
		x = 0;
	if (o & N)
		y = yroot - c->y - c->b;
	else if (o & S)
		y = c->y + c->h - c->b - yroot;
	else
		y = 0;
	XGrabPointer(dpy, c->frame, False,
	             ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, None, curs, CurrentTime);
	lasttime = 0;
	containerdecorate(c, NULL, NULL, 0, o);
	while (!XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ExposureMask, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				res = getwin(ev.xexpose.window);
				decorate(&res);
			}
			break;
		case ButtonRelease:
			goto done;
			break;
		case MotionNotify:
			if (x > c->w)
				x = 0;
			if (y > c->h)
				y = 0;
			if (o & W &&
			    ((ev.xmotion.x_root < xroot && x > ev.xmotion.x_root - c->nx) ||
			     (ev.xmotion.x_root > xroot && x < ev.xmotion.x_root - c->nx))) {
				dx = xroot - ev.xmotion.x_root;
				if (c->nw + dx >= wm.minsize) {
					c->nx -= dx;
					c->nw += dx;
				}
			} else if (o & E &&
			    ((ev.xmotion.x_root > xroot && x > c->nx + c->nw - ev.xmotion.x_root) ||
			     (ev.xmotion.x_root < xroot && x < c->nx + c->nw - ev.xmotion.x_root))) {
				dx = ev.xmotion.x_root - xroot;
				if (c->nw + dx >= wm.minsize) {
					c->nw += dx;
				}
			}
			if (o & N &&
			    ((ev.xmotion.y_root < yroot && y > ev.xmotion.y_root - c->ny) ||
			     (ev.xmotion.y_root > yroot && y < ev.xmotion.y_root - c->ny))) {
				dy = yroot - ev.xmotion.y_root;
				if (c->nh + dy >= wm.minsize) {
					c->ny -= dy;
					c->nh += dy;
				}
			} else if (o & S &&
			    ((ev.xmotion.y_root > yroot && c->ny + c->nh - ev.xmotion.y_root < y) ||
			     (ev.xmotion.y_root < yroot && c->ny + c->nh - ev.xmotion.y_root > y))) {
				dy = ev.xmotion.y_root - yroot;
				if (c->nh + dy >= wm.minsize) {
					c->nh += dy;
				}
			}
			if (ev.xmotion.time - lasttime > RESIZETIME) {
				containercalccols(c, 0, 1);
				containermoveresize(c);
				containerredecorate(c, NULL, NULL, o);
				lasttime = ev.xmotion.time;
			}
			xroot = ev.xmotion.x_root;
			yroot = ev.xmotion.y_root;
			break;
		}
	}
done:
	containercalccols(c, 0, 1);
	containermoveresize(c);
	containerdecorate(c, NULL, NULL, 0, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* move container with mouse */
static void
mousemove(struct Container *c, int xroot, int yroot, enum Octant o)
{
	struct Winres res;
	XEvent ev;
	int x, y;

	x = y = 0;
	containerdecorate(c, NULL, NULL, 0, o);
	XGrabPointer(dpy, c->frame, False,
	             ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, None, visual.cursors[CURSOR_MOVE], CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ExposureMask, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				res = getwin(ev.xexpose.window);
				decorate(&res);
			}
			break;
		case ButtonRelease:
			goto done;
			break;
		case MotionNotify:
			x = ev.xmotion.x_root - xroot;
			y = ev.xmotion.y_root - yroot;
			containerincrmove(c, x, y);
			xroot = ev.xmotion.x_root;
			yroot = ev.xmotion.y_root;
			break;
		}
	}
done:
	containerdecorate(c, NULL, NULL, 0, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* control placement of row with mouse */
static void
mousererow(struct Row *row)
{
	struct Container *c;
	struct Column *origcol, *col, *newcol, *prevcol;
	struct Row *prev, *r;
	struct Tab *t;
	struct Winres res;
	XEvent ev;
	Time lasttime;
	int dy, y, sumh;

	origcol = row->col;
	if (origcol->maxrow != NULL)
		return;
	prevcol = origcol;
	c = row->col->c;
	newcol = NULL;
	y = 0;
	lasttime = 0;
	buttonleftdecorate(row, 1);
	XRaiseWindow(dpy, row->bar);
	XGrabPointer(dpy, row->bar, False, ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ExposureMask, &ev)) {
		switch(ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				res = getwin(ev.xexpose.window);
				decorate(&res);
			}
			break;
		case MotionNotify:
			for (col = c->cols; col != NULL; col = col->next) {
				if (ev.xmotion.x_root >= c->x + col->x &&
				    ev.xmotion.x_root < c->x + col->x + col->w &&
				    ev.xmotion.y_root >= c->y + c->b &&
				    ev.xmotion.y_root < c->y + c->h - c->b - config.titlewidth) {
					newcol = col;
					y = ev.xmotion.y_root - c->y;
					if (prevcol != newcol) {
						row->col = col;
						rowcalctabs(row);
						for (t = row->tabs; t != NULL; t = t->next) {
							tabmoveresize(t);
							tabdecorate(t, 0);
						}
						row->col = origcol;
					}
					titlebarmoveresize(row, col->x, y, col->w);
					prevcol = col;
				}
			}
			break;
		case ButtonRelease:
			goto done;
		}
	}
done:
	sumh = c->b;
	prev = NULL;
	col = row->col;
	if (newcol != NULL) {
		for (r = newcol->rows; r != NULL; r = r->next) {
			sumh += row->h;
			prev = r;
			if (y < sumh)
				break;
		}
		if (prev != row && prev != NULL) {
			rowdetach(row, 0);
			coladdrow(newcol, row, prev);
		}
		if (row->prev != NULL) {
			dy = y - row->y;
			row->h -= dy;
			row->prev->h += dy;
		}
	}
	containercalccols(c, 0, 1);
	containermoveresize(c);
	buttonleftdecorate(row, 0);
	containerdecorate(c, NULL, NULL, 0, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* close tab with mouse */
static void
mouseclose(struct Row *row)
{
	struct Winres res;
	XEvent ev;

	buttonrightdecorate(row, 1);
	XGrabPointer(dpy, row->br, False, ButtonReleaseMask,
	             GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | ExposureMask, &ev)) {
		switch(ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				res = getwin(ev.xexpose.window);
				decorate(&res);
			}
			break;
		case ButtonRelease:
			if (ev.xbutton.window == row->br &&
			    ev.xbutton.x >= 0 && ev.xbutton.x >= 0 &&
			    ev.xbutton.x < config.titlewidth && ev.xbutton.x < config.titlewidth)
				winclose(row->seltab->ds != NULL ? row->seltab->ds->win : row->seltab->win);
			goto done;
			break;
		}
	}
done:
	buttonrightdecorate(row, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* resize tiles by dragging division with mouse */
static void
mouseretile(struct Container *c, struct Column *cdiv, struct Row *rdiv, int xroot, int yroot)
{
	struct Winres res;
	XEvent ev;
	Cursor curs;
	Time lasttime;
	int x, y;
	int update;

	if (cdiv != NULL && cdiv->next != NULL)
		curs = visual.cursors[CURSOR_H];
	else if (rdiv != NULL && rdiv->next != NULL && rdiv->col->maxrow == NULL)
		curs = visual.cursors[CURSOR_V];
	else
		return;
	x = y = 0;
	update = 0;
	lasttime = 0;
	containerdecorate(c, cdiv, rdiv, 0, 0);
	XGrabPointer(dpy, c->frame, False, ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, None, curs, CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ExposureMask, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				res = getwin(ev.xexpose.window);
				decorate(&res);
			}
			break;
		case ButtonRelease:
			goto done;
			break;
		case MotionNotify:
			x = ev.xmotion.x_root - xroot;
			y = ev.xmotion.y_root - yroot;
			if (cdiv != NULL) {
				if (x < 0 && cdiv->w + x > config.minsize) {
					cdiv->w += x;
					cdiv->next->w -= x;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				} else if (x > 0 && cdiv->next->w - x > config.minsize) {
					cdiv->next->w -= x;
					cdiv->w += x;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
			} else if (rdiv != NULL) {
				if (y < 0 && rdiv->h + y > config.minsize) {
					rdiv->h += y;
					rdiv->next->h -= y;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				} else if (y > 0 && rdiv->next->h - y > config.minsize) {
					rdiv->next->h -= y;
					rdiv->h += y;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
			}
			if (update) {
				containercalccols(c, 1, 1);
				containermoveresize(c);
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
	containermoveresize(c);
	containerdecorate(c, NULL, NULL, 0, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* stack rows in column with mouse */
static void
mousestack(struct Row *row)
{
	struct Winres res;
	XEvent ev;

	buttonleftdecorate(row, 1);
	XGrabPointer(dpy, row->bl, False, ButtonReleaseMask,
	             GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | ExposureMask, &ev)) {
		switch(ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				res = getwin(ev.xexpose.window);
				decorate(&res);
			}
			break;
		case ButtonRelease:
			if (row->col->nrows > 1 &&
			    ev.xbutton.window == row->bl &&
			    ev.xbutton.x >= 0 && ev.xbutton.x >= 0 &&
			    ev.xbutton.x < config.titlewidth && ev.xbutton.x < config.titlewidth) {
				rowstack(row->col, (row->col->maxrow == row) ? NULL : row);
				tabfocus(row->seltab, 0);
			}
			goto done;
			break;
		}
	}
done:
	buttonleftdecorate(row, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* handle mouse operation, focusing and raising */
static void
xeventbuttonpress(XEvent *e)
{
	struct Winres res;
	struct Monitor *mon;
	struct Container *c;
	struct Column *cdiv;
	struct Row *rdiv;
	struct Tab *t;
	enum Octant o;
	XButtonPressedEvent *ev;
	Window dw;
	int x, y;

	ev = &e->xbutton;
	res = getwin(ev->window);

	/* if user clicked in no window, focus the monitor below cursor */
	c = res.c;
	if (c == NULL) {
		mon = getmon(ev->x_root, ev->y_root);
		if (mon)
			deskfocus(mon->seldesk, 1);
		goto done;
	}

	if (res.t != NULL) {
		t = res.t;
	} else if (res.d != NULL) {
		t = res.d->t;
	} else if (res.row != NULL) {
		t = res.row->seltab;
	} else {
		t = c->selcol->selrow->seltab;
	}
	if (t == NULL) {
		goto done;
	}

	/* focus client */
	if ((wm.focused == NULL || t != wm.focused->selcol->selrow->seltab) && ev->button == Button1)
		tabfocus(t, 1);

	/* raise client */
	if ((c != wm.abovelist || c != wm.centerlist || c != wm.belowlist) && ev->button == Button1)
		containerraise(c);

	/* do action performed by mouse on non-maximized windows */
	if (XTranslateCoordinates(dpy, ev->window, c->frame, ev->x, ev->y, &x, &y, &dw) != True)
		goto done;
	o = getoctant(c, x, y);
	if (ev->window == t->title && ev->button == Button3) {
		mouseretab(t, ev->x_root, ev->y_root, ev->x, ev->y);
	} else if (res.row != NULL && ev->window == res.row->bl && ev->button == Button1) {
		mousestack(res.row);
	} else if (res.row != NULL && ev->window == res.row->bl && ev->button == Button3) {
		mousererow(res.row);
	} else if (res.row != NULL && ev->window == res.row->br && ev->button == Button1) {
		mouseclose(res.row);
	} else if (ev->window == c->frame && ev->button == Button1 && o == C) {
		getdivisions(c, &cdiv, &rdiv, x, y);
		if (cdiv != NULL || rdiv != NULL) {
			mouseretile(c, cdiv, rdiv, ev->x_root, ev->y_root);
		}
	} else if (!c->isfullscreen && !c->isminimized && !c->ismaximized) {
		if (ev->state == MODIFIER && ev->button == Button1) {
			mousemove(c, ev->x_root, ev->y_root, 0);
		} else if (ev->window == c->frame && ev->button == Button3) {
			mousemove(c, ev->x_root, ev->y_root, o);
		} else if ((ev->state == MODIFIER && ev->button == Button3) ||
		           (o != C && ev->window == c->frame && ev->button == Button1)) {
			if (o == C) {
				if (x >= c->w / 2 && y >= c->h / 2) {
					o = SE;
				}
				if (x >= c->w / 2 && y <= c->h / 2) {
					o = NE;
				}
				if (x <= c->w / 2 && y >= c->h / 2) {
					o = SW;
				}
				if (x <= c->w / 2 && y <= c->h / 2) {
					o = NW;
				}
			}
			mouseresize(c, ev->x_root, ev->y_root, o);
		} else if (ev->window == t->title && ev->button == Button1) {
			tabdecorate(t, 1);
			mousemove(c, ev->x_root, ev->y_root, 0);
			tabdecorate(t, 0);
		}
	}

done:
	XAllowEvents(dpy, ReplayPointer, CurrentTime);
}

/* handle client message event */
static void
xeventclientmessage(XEvent *e)
{
	struct Monitor *mon;
	struct Desktop *prevdesk, *desk;
	struct Container *c;
	struct Tab *t;
	struct Winres res;
	XClientMessageEvent *ev;
	XWindowChanges wc;
	unsigned value_mask = 0;
	int i;

	ev = &e->xclient;
	res = getwin(ev->window);
	if (ev->message_type == atoms[_NET_CURRENT_DESKTOP]) {
		deskfocus(getdesk(ev->data.l[0], ev->data.l[2]), 1);
	} else if (ev->message_type == atoms[_NET_SHOWING_DESKTOP]) {
		if (ev->data.l[0]) {
			deskshow(1);
		} else {
			deskfocus(wm.selmon->seldesk, 1);
		}
	} else if (ev->message_type == atoms[_NET_WM_STATE]) {
		if (res.c == NULL)
			return;
		if (((Atom)ev->data.l[1] == atoms[_NET_WM_STATE_MAXIMIZED_VERT] ||
		     (Atom)ev->data.l[1] == atoms[_NET_WM_STATE_MAXIMIZED_HORZ]) &&
		    ((Atom)ev->data.l[2] == atoms[_NET_WM_STATE_MAXIMIZED_VERT]  ||
		     (Atom)ev->data.l[2] == atoms[_NET_WM_STATE_MAXIMIZED_HORZ])) {
			containermaximize(res.c, ev->data.l[0]);
		}
		for (i = 1; i < 3; i++) {
			if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_FULLSCREEN]) {
				containerfullscreen(res.c, ev->data.l[0]);
			} else if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_SHADED]) {
				containershade(res.c, ev->data.l[0]);
			} else if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_STICKY]) {
				containerstick(res.c, ev->data.l[0]);
			} else if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_HIDDEN]) {
				containerminimize(res.c, ev->data.l[0], (res.c == wm.focused));
			} else if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_ABOVE]) {
				containerabove(res.c, ev->data.l[0]);
			} else if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_BELOW]) {
				containerbelow(res.c, ev->data.l[0]);
			} else if ((Atom)ev->data.l[i] == atoms[_NET_WM_STATE_DEMANDS_ATTENTION]) {
				tabupdateurgency(res.t, ev->data.l[0] == ADD || (ev->data.l[0] == TOGGLE && !res.t->isurgent));
			}
		}
	} else if (ev->message_type == atoms[_NET_ACTIVE_WINDOW]) {
		if (res.t == NULL && wm.focused != NULL) {
			res.c = wm.focused;
			res.t = wm.focused->selcol->selrow->seltab;
		}
		c = NULL;
		t = NULL;
		switch (ev->data.l[3]) {
		case _SHOD_FOCUS_LEFT_CONTAINER:
		case _SHOD_FOCUS_RIGHT_CONTAINER:
		case _SHOD_FOCUS_TOP_CONTAINER:
		case _SHOD_FOCUS_BOTTOM_CONTAINER:
			if (res.c != NULL && (c = getfocusedbydirection(res.c, ev->data.l[3])) != NULL)
				t = c->selcol->selrow->seltab;
			break;
		case _SHOD_FOCUS_PREVIOUS_CONTAINER:
			if (res.c != NULL && res.c->fprev != NULL) {
				c = res.c->fprev;
				t = c->selcol->selrow->seltab;
			} else if (ev->data.l[4] && res.c != NULL && (c = getlastfocused(res.c)) != NULL) {
				t = c->selcol->selrow->seltab;
			}
			break;
		case _SHOD_FOCUS_NEXT_CONTAINER:
			if (res.c != NULL && res.c->fnext != NULL) {
				c = res.c->fnext;
				t = c->selcol->selrow->seltab;
			} else if (ev->data.l[4] && res.c != NULL && (c = getfirstfocused(res.c)) != NULL) {
				t = c->selcol->selrow->seltab;
			}
			break;
		case _SHOD_FOCUS_LEFT_WINDOW:
			if (res.t != NULL && res.t->row->col->prev != NULL) {
				c = res.c;
				t = res.t->row->col->prev->selrow->seltab;
			}
			break;
		case _SHOD_FOCUS_RIGHT_WINDOW:
			if (res.t != NULL && res.t->row->col->next != NULL) {
				c = res.c;
				t = res.t->row->col->next->selrow->seltab;
			}
			break;
		case _SHOD_FOCUS_TOP_WINDOW:
			if (res.t != NULL && res.t->row->prev != NULL) {
				c = res.c;
				t = res.t->row->prev->seltab;
			}
			break;
		case _SHOD_FOCUS_BOTTOM_WINDOW:
			if (res.t != NULL && res.t->row->next != NULL) {
				c = res.c;
				t = res.t->row->next->seltab;
			}
			break;
		case _SHOD_FOCUS_PREVIOUS_WINDOW:
			c = res.c;
			if (res.t != NULL && res.t->prev != NULL) {
				t = res.t->prev;
			} else if (ev->data.l[4] && res.t != NULL) {
				for (t = res.t->row->tabs; t != NULL; t = t->next) {
					if (t->next == NULL) {
						break;
					}
				}
			}
			break;
		case _SHOD_FOCUS_NEXT_WINDOW:
			c = res.c;
			if (res.t != NULL && res.t->next != NULL) {
				t = res.t->next;
			} else if (ev->data.l[4] && res.t != NULL) {
				t = res.t->row->tabs;
			}
			break;
		default:
			c = res.c;
			t = res.t;
			break;
		}
		if (c == NULL || t == NULL)
			return;
		containerraise(c);
		tabfocus(t, 1);
	} else if (ev->message_type == atoms[_NET_CLOSE_WINDOW]) {
		winclose(ev->window);
	} else if (ev->message_type == atoms[_NET_MOVERESIZE_WINDOW]) {
		value_mask = 0;
		if (res.c == NULL)
			return;
		value_mask = CWX | CWY | CWWidth | CWHeight;
		wc.x = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? res.c->x + ev->data.l[1] : ev->data.l[1];
		wc.y = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? res.c->y + ev->data.l[2] : ev->data.l[2];
		wc.width = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? res.c->w + ev->data.l[3] : ev->data.l[3];
		wc.height = (ev->data.l[0] & _SHOD_MOVERESIZE_RELATIVE) ? res.c->h + ev->data.l[4] : ev->data.l[4];
		if (res.d != NULL) {
			dialogconfigure(res.d, value_mask, &wc);
		} else {
			containerconfigure(res.c, value_mask, &wc);
		}
	} else if (ev->message_type == atoms[_NET_WM_DESKTOP]) {
		if (res.c == NULL)
			return;
		if (ev->data.l[0] == 0xFFFFFFFF) {
			containerstick(res.c, ADD);
		} else if (!res.c->isminimized) {
			if ((desk = getdesk(ev->data.l[0], ev->data.l[2])) == NULL || desk == res.c->desk)
				return;
			if (res.c->issticky)
				containerstick(res.c, REMOVE);
			mon = res.c->mon;
			prevdesk = res.c->desk;
			containersendtodesk(res.c, desk, 0, 0);
			c = getnextfocused(mon, prevdesk);
			if (c != NULL) {
				tabfocus(c->selcol->selrow->seltab, 0);
			}
		}
	} else if (ev->message_type == atoms[_NET_REQUEST_FRAME_EXTENTS]) {
		if (res.c == NULL) {
			/*
			 * A client can request an estimate for the frame size
			 * which the window manager will put around it before
			 * actually mapping its window. Java does this (as of
			 * openjdk-7).
			 */
			ewmhsetframeextents(ev->window, config.borderwidth, config.titlewidth);
		} else {
			ewmhsetframeextents(ev->window, res.c->b, (res.c->isfullscreen && res.c->ncols == 1 && res.c->cols->nrows == 1) ? 0 : config.titlewidth);
		}
	} else if (ev->message_type == atoms[_NET_WM_MOVERESIZE]) {
		/*
		 * Client-side decorated Gtk3 windows emit this signal when being
		 * dragged by their GtkHeaderBar
		 */
		if (res.c == NULL)
			return;
		switch (ev->data.l[2]) {
		case _NET_WM_MOVERESIZE_CANCEL:
			XUngrabPointer(dpy, CurrentTime);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], NW);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOP:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], N);
			break;
		case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], NE);
			break;
		case _NET_WM_MOVERESIZE_SIZE_RIGHT:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], E);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], SE);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOM:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], S);
			break;
		case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], SW);
			break;
		case _NET_WM_MOVERESIZE_SIZE_LEFT:
			mouseresize(res.c, ev->data.l[0], ev->data.l[1], W);
			break;
		case _NET_WM_MOVERESIZE_MOVE:
			mousemove(res.c, ev->data.l[0], ev->data.l[1], C);
			break;
		default:
			return;
		}
	}
}

/* handle configure notify event */
static void
xeventconfigurenotify(XEvent *e)
{
	XConfigureEvent *ev;

	ev = &e->xconfigure;
	if (ev->window == root) {
		screenw = ev->width;
		screenh = ev->height;
		monupdate();
		notifplace();
	}
}

/* handle configure request event */
static void
xeventconfigurerequest(XEvent *e)
{
	XConfigureRequestEvent *ev;
	XWindowChanges wc;
	struct Winres res;

	ev = &e->xconfigurerequest;
	wc.x = ev->x;
	wc.y = ev->y;
	wc.width = ev->width;
	wc.height = ev->height;
	wc.border_width = ev->border_width;
	wc.sibling = ev->above;
	wc.stack_mode = ev->detail;
	res = getwin(ev->window);
	if (res.d != NULL) {
		dialogconfigure(res.d, ev->value_mask, &wc);
	} else if (res.c != NULL) {
		if (cflag) {
			containerconfigure(res.c, ev->value_mask, &wc);
		} else {
			containermoveresize(res.c);
		}
	} else if (res.c == NULL){
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
}

/* forget about client */
static void
xeventdestroynotify(XEvent *e)
{
	XDestroyWindowEvent *ev;
	struct Winres res;

	ev = &e->xdestroywindow;
	res = getwin(ev->window);
	if (res.dapp && ev->window == res.dapp->win) {
		dockappdel(res.dapp);
	} else if (res.n && ev->window == res.n->win) {
		notifdel(res.n);
		return;
	} else if (res.bar != NULL && ev->window == res.bar->win) {
		bardel(res.bar);
		return;
	} else if (res.d && ev->window == res.d->win) {
		dialogdel(res.d);
	} else if (res.t && ev->window == res.t->win) {
		unmanage(res.t);
	}
	ewmhsetclients();
	ewmhsetclientsstacking();
}

/* redraw window decoration */
static void
xeventexpose(XEvent *e)
{
	XExposeEvent *ev;
	struct Winres res;

	ev = &e->xexpose;
	if (ev->count == 0) {
		res = getwin(ev->window);
		decorate(&res);
	}
}

/* handle focusin event */
static void
xeventfocusin(XEvent *e)
{
	XFocusChangeEvent *ev;
	struct Winres res;

	ev = &e->xfocus;
	res = getwin(ev->window);
	if (wm.focused == NULL) {
		tabfocus(NULL, 0);
	} else if (wm.focused != res.c) {
		tabfocus(wm.focused->selcol->selrow->seltab, 1);
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
	manage(ev->window, &wa, 0);
}

/* update client properties */
static void
xeventpropertynotify(XEvent *e)
{
	XPropertyEvent *ev;
	struct Winres res;

	ev = &e->xproperty;
	if (ev->state == PropertyDelete)
		return;
	res = getwin(ev->window);
	if (res.t == NULL || ev->window != res.t->win)
		return;
	if (ev->atom == XA_WM_NAME || ev->atom == atoms[_NET_WM_NAME]) {
		tabupdatetitle(res.t);
		tabdecorate(res.t, 0);
	} else if (ev->atom == XA_WM_HINTS) {
		tabupdateurgency(res.t, winisurgent(res.t->win));
	} else if (res.bar != NULL && (ev->atom == _NET_WM_STRUT_PARTIAL || ev->atom == _NET_WM_STRUT)) {
		barstrut(res.bar);
		monupdatearea();
	}
}

/* forget about client */
static void
xeventunmapnotify(XEvent *e)
{
	XUnmapEvent *ev;
	struct Winres res;

	ev = &e->xunmap;
	res = getwin(ev->window);
	if (res.bar != NULL && ev->window == res.bar->win) {
		bardel(res.bar);
		return;
	} else if (res.n && ev->window == res.n->win) {
		notifdel(res.n);
		return;
	} else if (res.dapp && ev->window == res.dapp->win) {
		if (res.dapp->ignoreunmap) {
			res.dapp->ignoreunmap--;
		} else {
			dockappdel(res.dapp);
		}
		return;
	} else if (res.d && ev->window == res.d->win) {
		if (res.d->ignoreunmap) {
			res.d->ignoreunmap--;
			return;
		} else {
			dialogdel(res.d);
		}
	} else if (res.t && ev->window == res.t->win) {
		if (res.t->ignoreunmap) {
			res.t->ignoreunmap--;
			return;
		} else {
			unmanage(res.t);
		}
	}
	ewmhsetclients();
	ewmhsetclientsstacking();
}

/* destroy dummy windows */
static void
cleandummywindows(void)
{
	int i;

	XDestroyWindow(dpy, wm.wmcheckwin);
	XDestroyWindow(dpy, wm.focuswin);
	for (i = 0; i < LAYER_LAST; i++) {
		XDestroyWindow(dpy, wm.layerwins[i]);
	}
}

/* free cursors */
static void
cleancursors(void)
{
	size_t i;

	for (i = 0; i < CURSOR_LAST; i++) {
		XFreeCursor(dpy, visual.cursors[i]);
	}
}

/* clean window manager structures */
static void
cleanwm(void)
{
	while (wm.c != NULL) {
		containerdel(wm.c);
	}
	while (wm.nhead != NULL) {
		notifdel(wm.nhead);
	}
	while (wm.bars != NULL) {
		bardel(wm.bars);
	}
	while (wm.monhead != NULL) {
		mondel(wm.monhead);
	}
}

/* clean dock */
static void
cleandock(void)
{
	while (dock.head != NULL) {
		dockappdel(dock.head);
	}
	if (dock.pix != None) {
		XFreePixmap(dpy, dock.pix);
	}
	XDestroyWindow(dpy, dock.win);
}

/* free fontset */
static void
cleanfontset(void)
{
	XFreeFontSet(dpy, visual.fontset);
}

/* shod window manager */
int
main(int argc, char *argv[])
{
	XEvent ev;
	void (*xevents[LASTEvent])(XEvent *) = {
		[ButtonPress]      = xeventbuttonpress,
		[ClientMessage]    = xeventclientmessage,
		[ConfigureNotify]  = xeventconfigurenotify,
		[ConfigureRequest] = xeventconfigurerequest,
		[DestroyNotify]    = xeventdestroynotify,
		[Expose]           = xeventexpose,
		[FocusIn]          = xeventfocusin,
		[MapRequest]       = xeventmaprequest,
		[PropertyNotify]   = xeventpropertynotify,
		[UnmapNotify]      = xeventunmapnotify
	};

	/* get configuration */
	getoptions(argc, argv);

	/* open connection to server and set X variables */
	if (!setlocale(LC_ALL, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	screenw = DisplayWidth(dpy, screen);
	screenh = DisplayHeight(dpy, screen);
	depth = DefaultDepth(dpy, screen);
	root = RootWindow(dpy, screen);
	gc = XCreateGC(dpy, root, GCFillStyle, &(XGCValues){.fill_style = FillSolid});
	xerrorxlib = XSetErrorHandler(xerror);
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);

	/* get configuration */
	getresources();
	getoptions(argc, argv);

	/* initialize */
	initsignal();
	initdummywindows();
	initfontset();
	initcursors();
	initatoms();
	initroot();
	initvisual();
	initdock();

	/* set up list of monitors */
	monupdate();
	wm.selmon = wm.monhead;

	/* initialize ewmh hints */
	ewmhinit();
	ewmhsetcurrentdesktop(0);
	ewmhsetshowingdesktop(0);
	ewmhsetclients();
	ewmhsetclientsstacking();
	ewmhsetactivewindow(None);

	/* scan windows */
	scan();
	mapfocuswin();

	/* run main event loop */
	while (running && !XNextEvent(dpy, &ev))
		if (xevents[ev.type])
			(*xevents[ev.type])(&ev);

	/* clean up */
	cleandummywindows();
	cleancursors();
	cleanfontset();
	cleanwm();
	cleandock();

	/* clear ewmh hints */
	ewmhsetclients();
	ewmhsetclientsstacking();
	ewmhsetactivewindow(None);

	/* close connection to server */
	XUngrabPointer(dpy, CurrentTime);
	XCloseDisplay(dpy);

	return 0;
}
