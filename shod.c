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
#include "theme.xpm"

#define WMNAME          "shod"
#define DIV             15      /* number to divide the screen into grids */
#define FONT            "-misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1"
#define MODIFIER        Mod1Mask
#define FOCUS_BUTTONS   1
#define RAISE_BUTTONS   1
#define NDESKTOPS       10
#define NOTIFGRAVITY    "NE"
#define NOTIFGAP        3
#define IGNOREUNMAP     6       /* number of unmap notifies to ignore while scanning existing clients */
#define NAMEMAXLEN      1024    /* maximum length of window's name */
#define DROPPIXELS      30      /* number of pixels from the border where a tab can be dropped in */
#define RESIZETIME      64      /* time to redraw containers during resizing */

#define _SHOD_RELATIVE_X        ((long)(1 << 16))
#define _SHOD_RELATIVE_Y        ((long)(1 << 17))
#define _SHOD_RELATIVE_WIDTH    ((long)(1 << 18))
#define _SHOD_RELATIVE_HEIGHT   ((long)(1 << 19))

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

/* notification spawning direction */
enum {
	DOWNWARDS,
	UPWARDS
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
	Window frame;                           /* where tab frames are */
	Window bar;                             /* title bar frame */
	Window bl;                              /* left button */
	Window br;                              /* right button */
	Pixmap pixbar;                          /* pixmap for the title bar */
	Pixmap pixbl;                           /* pixmap for left button */
	Pixmap pixbr;                           /* pixmap for right button */
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
	Window curswin;                         /* dummy window used for change cursor while hovering borders */
	Window frame;                           /* window to reparent the contents of the container */
	Pixmap pix;                             /* pixmap to draw the frame */
	int ncols;                              /* number of columns */
	int ismaximized, isminimized, issticky; /* window states */
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

struct Dock {
	struct Dock *prev, *next;               /* pointers for list of docks */
	Window win;                             /* dock window */
	int strut[STRUT_LAST];                  /* strut values */
	int partial;                            /* strut has 12 elements rather than 4 */
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
};

/* decoration pixmaps and colors */
struct Decor {
	Pixmap bl;                              /* left button */
	Pixmap br;                              /* right button */
	Pixmap tl;                              /* title left end */
	Pixmap t;                               /* title middle */
	Pixmap tr;                              /* title right end */
	Pixmap nw;                              /* northwest corner */
	Pixmap nf;                              /* north first edge */
	Pixmap n;                               /* north border */
	Pixmap nl;                              /* north last edge */
	Pixmap ne;                              /* northeast corner */
	Pixmap wf;                              /* west first edge */
	Pixmap w;                               /* west border */
	Pixmap wl;                              /* west last edge */
	Pixmap ef;                              /* east first edge */
	Pixmap e;                               /* east border */
	Pixmap el;                              /* east last edge */
	Pixmap sw;                              /* southwest corner */
	Pixmap sf;                              /* south first edge */
	Pixmap s;                               /* south border */
	Pixmap sl;                              /* south last edge */
	Pixmap se;                              /* southeast corner */
	unsigned long fg;                       /* foreground color */
	unsigned long bg;                       /* background color */
};

/* cursors, fonts, decorations, and decoration sizes */
struct Visual {
	struct Decor decor[STYLE_LAST][DECOR_LAST];
	Cursor cursors[CURSOR_LAST];
	XFontSet fontset;
	int edge;                               /* size of the decoration edge */
	int corner;                             /* size of the decoration corner */
	int border;                             /* size of the decoration border */
	int center;                             /* size of the decoration center */
	int division;                           /* size of the decoration division */
	int button;                             /* size of the buttons (actually, it's equal to .tab) */
	int tab;                                /* height of the tab bar */
};

/* window manager stuff */
struct WM {
	struct Dock *docks;                     /* list of docks */
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
};

/* configuration */
struct Config {
	unsigned int modifier;
	unsigned int focusbuttons;
	unsigned int raisebuttons;
	int ndesktops;
	int notifgap;
	const char *font;
	const char *notifgravity;

	/* the following elements are computed from elements above */
	int gravity;
	int direction;
};

/* global variables */
static XSetWindowAttributes clientswa = {
	.event_mask = EnterWindowMask | SubstructureNotifyMask | ExposureMask
		    | SubstructureRedirectMask | ButtonPressMask | FocusChangeMask
		    | PointerMotionMask
};
static int (*xerrorxlib)(Display *, XErrorEvent *);
static Display *dpy;
static Window root;
static GC gc;
static int depth;
static int screen, screenw, screenh;
static Atom atoms[ATOM_LAST];
static struct Visual visual;
static struct WM wm;
static struct Config config;
volatile sig_atomic_t running = 1;

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: shod [-d ndesks] [-f buttons] [-m modifier]\n");
	(void)fprintf(stderr, "            [-n notificationspec] [-r buttons]\n");
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
	    (e->request_code == X_GrabButton && e->error_code == BadAccess) ||
	    (e->request_code == X_GrabKey && e->error_code == BadAccess) ||
	    (e->request_code == X_CopyArea && e->error_code == BadDrawable) ||
	    (e->request_code == 139 && e->error_code == BadDrawable) ||
	    (e->request_code == 139 && e->error_code == 143))
		return 0;

	errx(1, "Fatal request. Request code=%d, error code=%d", e->request_code, e->error_code);
	return xerrorxlib(dpy, e);
}

/* create and copy pixmap */
static Pixmap
copypixmap(Pixmap src, int sx, int sy, int w, int h)
{
	Pixmap pix;

	pix = XCreatePixmap(dpy, root, w, h, depth);
	XCopyArea(dpy, src, pix, gc, sx, sy, w, h, 0, 0);
	return pix;
}

/* parse buttons string */
static unsigned int
parsebuttons(const char *s)
{
	const char *origs;
	unsigned int buttons;

	origs = s;
	buttons = 0;
	while (*s != '\0') {
		if (*s < '1' || *s > '5')
			errx(1, "improper buttons string %s", origs);
		buttons |= 1 << (*s - '1');
		s++;
	}
	return buttons;
}

/* parse modifier string */
static unsigned int
parsemodifier(const char *s)
{
	if (strcasecmp(s, "Mod1") == 0)
		return Mod1Mask;
	else if (strcasecmp(s, "Mod2") == 0)
		return Mod2Mask;
	else if (strcasecmp(s, "Mod3") == 0)
		return Mod3Mask;
	else if (strcasecmp(s, "Mod4") == 0)
		return Mod4Mask;
	else if (strcasecmp(s, "Mod5") == 0)
		return Mod5Mask;
	else
		errx(1, "improper modifier string %s", s);
	return 0;
}

/* stop running */
static void
siginthandler(int signo)
{
	(void)signo;
	running = 0;
}

/* read command-line options */
static void
getoptions(int argc, char *argv[])
{
	long n;
	int c;
	char *s;

	config.font = FONT;
	config.notifgravity = NOTIFGRAVITY;
	config.notifgap = NOTIFGAP;
	config.ndesktops = NDESKTOPS;
	config.modifier = MODIFIER;
	config.focusbuttons = FOCUS_BUTTONS;
	config.raisebuttons = RAISE_BUTTONS;

	while ((c = getopt(argc, argv, "d:f:m:n:r:")) != -1) {
		switch (c) {
		case 'd':
			if ((n = strtol(optarg, NULL, 10)) > 0)
				config.ndesktops = n;
			break;
		case 'f':
			config.focusbuttons = parsebuttons(optarg);
			break;
		case 'm':
			config.modifier = parsemodifier(optarg);
			break;
		case 'n':
			config.notifgravity = optarg;
			if ((s = strchr(optarg, ':')) == NULL)
				break;
			*(s++) = '\0';
			if ((n = strtol(s, NULL, 10)) > 0)
				config.notifgap = n;
			break;
		case 'r':
			config.raisebuttons = parsebuttons(optarg);
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

/* initialize gravity and direction values for notifications */
static void
initnotif(void)
{
	if (config.notifgravity == NULL || strcmp(config.notifgravity, "NE") == 0) {
		config.gravity = NorthEastGravity;
		config.direction = DOWNWARDS;
	} else if (strcmp(config.notifgravity, "NW") == 0) {
		config.gravity = NorthWestGravity;
		config.direction = DOWNWARDS;
	} else if (strcmp(config.notifgravity, "SW") == 0) {
		config.gravity = SouthWestGravity;
		config.direction = UPWARDS;
	} else if (strcmp(config.notifgravity, "SE") == 0) {
		config.gravity = SouthEastGravity;
		config.direction = UPWARDS;
	} else if (strcmp(config.notifgravity, "N") == 0) {
		config.gravity = NorthGravity;
		config.direction = DOWNWARDS;
	} else if (strcmp(config.notifgravity, "W") == 0) {
		config.gravity = WestGravity;
		config.direction = DOWNWARDS;
	} else if (strcmp(config.notifgravity, "C") == 0) {
		config.gravity = CenterGravity;
		config.direction = DOWNWARDS;
	} else if (strcmp(config.notifgravity, "E") == 0) {
		config.gravity = EastGravity;
		config.direction = DOWNWARDS;
	} else if (strcmp(config.notifgravity, "S") == 0) {
		config.gravity = SouthGravity;
		config.direction = UPWARDS;
	} else {
		errx(1, "unknown gravity %s", config.notifgravity);
	}
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

/* initialize decoration pixmap */
static void
inittheme(void)
{
	XGCValues val;
	XpmAttributes xa;
	XImage *img;
	Pixmap pix;
	struct Decor *d;
	unsigned int size;       /* size of each square in the .xpm file */
	unsigned int x, y;
	unsigned int i, j;
	int status;

	memset(&xa, 0, sizeof xa);
	status = XpmCreateImageFromData(dpy, theme, &img, NULL, &xa);
	if (status != XpmSuccess)
		errx(1, "could not load theme");

	/* create Pixmap from XImage */
	pix = XCreatePixmap(dpy, root, img->width, img->height, img->depth);
	val.foreground = 1;
	val.background = 0;
	XChangeGC(dpy, gc, GCForeground | GCBackground, &val);
	XPutImage(dpy, pix, gc, img, 0, 0, 0, 0, img->width, img->height);

	/* check whether the theme has the correct proportions and hotspots */
	size = 0;
	if (xa.valuemask & (XpmSize | XpmHotspot) &&
	    xa.width % 3 == 0 && xa.height % 3 == 0 && xa.height == xa.width &&
	    (xa.width / 3) % 2 == 1 && (xa.height / 3) % 2 == 1 &&
	    xa.x_hotspot < ((xa.width / 3) - 1) / 2) {
		size = xa.width / 3;
		visual.division = visual.border = xa.x_hotspot;
		visual.button = visual.tab = xa.y_hotspot;
		visual.corner = visual.border + visual.tab;
		visual.edge = (size - 1) / 2 - visual.corner;
		visual.center = size - visual.border * 2;
	}
	if (size == 0) {
		XDestroyImage(img);
		XFreePixmap(dpy, pix);
		errx(1, "theme in wrong format");
	}

	/* destroy pixmap into decoration parts and copy them into the decor array */
	y = 0;
	for (i = 0; i < STYLE_LAST; i++) {
		x = 0;
		for (j = 0; j < DECOR_LAST; j++) {
			d = &visual.decor[i][j];
			d->bl = copypixmap(pix, x + visual.border, y + visual.border, visual.button, visual.button);
			d->tl = copypixmap(pix, x + visual.border + visual.button, y + visual.border, visual.edge, visual.tab);
			d->t  = copypixmap(pix, x + visual.border + visual.button + visual.edge, y + visual.border, 1, visual.tab);
			d->tr = copypixmap(pix, x + visual.border + visual.button + visual.edge + 1, y + visual.border, visual.edge, visual.tab);
			d->br = copypixmap(pix, x + visual.border + visual.button + 2 * visual.edge + 1, y + visual.border, visual.button, visual.button);
			d->nw = copypixmap(pix, x, y, visual.corner, visual.corner);
			d->nf = copypixmap(pix, x + visual.corner, y, visual.edge, visual.border);
			d->n  = copypixmap(pix, x + visual.corner + visual.edge, y, 1, visual.border);
			d->nl = copypixmap(pix, x + visual.corner + visual.edge + 1, y, visual.edge, visual.border);
			d->ne = copypixmap(pix, x + size - visual.corner, y, visual.corner, visual.corner);
			d->wf = copypixmap(pix, x, y + visual.corner, visual.border, visual.edge);
			d->w  = copypixmap(pix, x, y + visual.corner + visual.edge, visual.border, 1);
			d->wl = copypixmap(pix, x, y + visual.corner + visual.edge + 1, visual.border, visual.edge);
			d->ef = copypixmap(pix, x + size - visual.border, y + visual.corner, visual.border, visual.edge);
			d->e  = copypixmap(pix, x + size - visual.border, y + visual.corner + visual.edge, visual.border, 1);
			d->el = copypixmap(pix, x + size - visual.border, y + visual.corner + visual.edge + 1, visual.border, visual.edge);
			d->sw = copypixmap(pix, x, y + size - visual.corner, visual.corner, visual.corner);
			d->sf = copypixmap(pix, x + visual.corner, y + size - visual.border, visual.edge, visual.border);
			d->s  = copypixmap(pix, x + visual.corner + visual.edge, y + size - visual.border, 1, visual.border);
			d->sl = copypixmap(pix, x + visual.corner + visual.edge + 1, y + size - visual.border, visual.edge, visual.border);
			d->se = copypixmap(pix, x + size - visual.corner, y + size - visual.corner, visual.corner, visual.corner);
			d->fg = XGetPixel(img, x + size / 2, y + visual.corner + visual.edge);
			d->bg = XGetPixel(img, x + size / 2, y + visual.border + visual.tab / 2);
			x += size;
		}
		y += size;
	}

	XDestroyImage(img);
	XFreePixmap(dpy, pix);
}

/* get pointer to proper structure given a window */
static struct Winres
getwin(Window win)
{
	struct Winres res;
	struct Dock *dock;
	struct Container *c;
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	struct Notification *n;

	res.dock = NULL;
	res.row = NULL;
	res.n = NULL;
	res.c = NULL;
	res.t = NULL;
	res.d = NULL;
	for (dock = wm.docks; dock != NULL; dock = dock->next) {
		if (win == dock->win) {
			res.dock = dock;
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
		if (win == c->frame || win == c->curswin) {
			res.c = c;
			return res;
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

/* get next focused container in given direction from another */
static struct Container *
getfocusedbydirection(struct Container *rel, int dir)
{
	struct Monitor *mon;
	struct Desktop *desk;
	struct Container *c, *ret;

	ret = NULL;
	mon = rel->mon;
	desk = rel->issticky ? rel->mon->seldesk : rel->desk;
	for (c = wm.focuslist; c != NULL; c = c->fnext) {
		if (c == rel || c->isminimized)
			continue;
		if (c->mon == mon && (c->issticky || c->desk == desk)) {
			switch (dir) {
			case _SHOD_FOCUS_LEFT_CONTAINER:
				if (c->x <= rel->x && (ret == NULL || c->x > ret->x))
					ret = c;
				break;
			case _SHOD_FOCUS_RIGHT_CONTAINER:
				if (c->x >= rel->x && (ret == NULL || c->x < ret->x))
					ret = c;
				break;
			case _SHOD_FOCUS_TOP_CONTAINER:
				if (c->y <= rel->y && (ret == NULL || c->y > ret->y))
					ret = c;
				break;
			case _SHOD_FOCUS_BOTTOM_CONTAINER:
				if (c->y >= rel->y && (ret == NULL || c->y < ret->y))
					ret = c;
				break;
			default:
				return NULL;
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
	if ((y < c->b && x <= visual.corner) || (x < c->b && y <= visual.corner)) {
		return NW;
	} else if ((y < c->b && x >= c->w - visual.corner) || (x > c->w - c->b && y <= visual.corner)) {
		return NE;
	} else if ((y > c->h - c->b && x <= visual.corner) || (x < c->b && y >= c->h - visual.corner)) {
		return SW;
	} else if ((y > c->h - c->b && x >= c->w - visual.corner) || (x > c->w - c->b && y >= c->h - visual.corner)) {
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
		if (col->next != NULL && x >= col->x + col->w && x < col->x + col->w + visual.division) {
			*cdiv = col;
			return;
		}
		if (x >= col->x && x < col->x + col->w) {
			for (row = col->rows; row != NULL; row = row->next) {
				if (row->next != NULL && y >= row->y + row->h && y < row->y + row->h + visual.division) {
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
	XChangeProperty(dpy, wm.wmcheckwin, atoms[_NET_WM_NAME], atoms[UTF8_STRING], 8, PropModeReplace, (unsigned char *) WMNAME, sizeof(WMNAME)-1);
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
	Atom data[8];
	int n = 0;

	if (c == NULL)
		return;
	if (c == wm.focused)
		data[n++] = atoms[_NET_WM_STATE_FOCUSED];
	if (c->isfullscreen)
		data[n++] = atoms[_NET_WM_STATE_FULLSCREEN];
	if (c->issticky)
		data[n++] = atoms[_NET_WM_STATE_STICKY];
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

/* calculate size of dialogs of a tab */
static void
dialogcalcsize(struct Dialog *d)
{
	struct Tab *t;

	t = d->t;
	d->w = max(1, min(d->maxw, t->winw - 2 * visual.border));
	d->h = max(1, min(d->maxh, t->winh - 2 * visual.border));
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

	x = visual.button;
	for (i = 0, t = row->tabs; t != NULL; t = t->next, i++) {
		if (row == row->col->maxrow)
			t->winh = max(1, row->col->c->h - 2 * row->col->c->b - row->col->nrows * visual.tab);
		else
			t->winh = max(1, row->h - visual.tab);
		t->winw = row->col->w;
		t->w = max(1, ((i + 1) * (t->winw - 2 * visual.button) / row->ntabs) - (i * (t->winw - 2 * visual.button) / row->ntabs));
		t->x = x;
		x += t->w;
		for (d = t->ds; d != NULL; d = d->next) {
			dialogcalcsize(d);
		}
	}
}

/* calculate position and height of rows of a column */
static void
colcalcrows(struct Column *col, int recursive)
{
	struct Container *c;
	struct Row *row;
	int i, y, h, sumh;

	c = col->c;

	/* check if rows sum up the height of the container */
	sumh = 0;
	for (row = col->rows; row != NULL; row = row->next) {
		if (row->h <= visual.tab) {
			sumh = 0;
			break;
		}
		sumh += row->h;
	}
	sumh += (col->nrows - 1) * visual.division + 2 * c->b;

	if (col->c->isfullscreen && col->c->ncols == 1 && col->nrows == 1) {
		h = col->c->h + visual.tab;
		y = -visual.tab;
	} else {
		h = col->c->h - 2 * c->b - (col->nrows - 1) * visual.division;
		y = c->b;
	}
	for (i = 0, row = col->rows; row != NULL; row = row->next, i++) {
		if (sumh != c->h)
			row->h = max(1, ((i + 1) * h / col->nrows) - (i * h / col->nrows));
		row->y = y;
		y += row->h + visual.division;
		if (recursive) {
			rowcalctabs(row);
		}
	}
}

/* calculate position and width of columns of a container */
static void
containercalccols(struct Container *c, int recursive)
{
	struct Column *col;
	int i, x, w, sumw;

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
		c->b = visual.border;
	} else {
		c->x = c->nx;
		c->y = c->ny;
		c->w = c->nw;
		c->h = c->nh;
		c->b = visual.border;
	}

	/* check if columns sum up the width of the container */
	sumw = 0;
	for (col = c->cols; col != NULL; col = col->next) {
		sumw += col->w;
	}
	sumw += (c->ncols - 1) * visual.division + 2 * c->b;

	w = c->w - 2 * c->b - (c->ncols - 1) * visual.division;
	x = c->b;
	for (i = 0, col = c->cols; col != NULL; col = col->next, i++) {
		if (sumw != c->w)
			col->w = max(1, ((i + 1) * w / c->ncols) - (i * w / c->ncols));
		col->x = x;
		x += col->w + visual.division;
		if (recursive) {
			colcalcrows(col, 1);
		}
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
	c->nw = max(visual.center + c->b * 2, w);
	c->nh = max(visual.center + c->b * 2, h);

	/* if the user placed the window, we should not re-place it */
	if (userplaced)
		return;

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
	containercalccols(c, 1);
}

/* decorate dialog window */
static void
dialogdecorate(struct Dialog *d)
{
	XGCValues val;
	struct Decor *decor;    /* unpressed decoration */
	int fullw, fullh;       /* size of dialog window + borders */
	int partw, parth;       /* size of dialog window + borders - corners */

	decor = &visual.decor[tabgetstyle(d->t)][DIALOG];
	fullw = d->w + 2 * visual.border;
	fullh = d->h + 2 * visual.border;
	partw = fullw - 2 * visual.corner;
	parth = fullh - 2 * visual.corner;

	/* (re)create pixmap */
	if (d->pw != fullw || d->ph != fullh || d->pix == None) {
		if (d->pix != None)
			XFreePixmap(dpy, d->pix);
		d->pix = XCreatePixmap(dpy, d->frame, fullw, fullh, depth);
	}
	d->pw = fullw;
	d->ph = fullh;

	val.fill_style = FillTiled;
	val.tile = decor->w;
	val.ts_x_origin = 0;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, d->pix, gc, 0, visual.corner, visual.border, parth);

	val.tile = decor->e;
	val.ts_x_origin = visual.border + d->w;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, d->pix, gc, visual.border + d->w, visual.corner, visual.border, parth);

	val.tile = decor->n;
	val.ts_x_origin = 0;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, d->pix, gc, visual.corner, 0, partw, visual.border);

	val.tile = decor->s;
	val.ts_x_origin = 0;
	val.ts_y_origin = visual.border + d->h;
	XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, d->pix, gc, visual.corner, visual.border + d->h, partw, visual.border);

	XCopyArea(dpy, decor->nw, d->pix, gc, 0, 0, visual.corner, visual.corner, 0, 0);
	XCopyArea(dpy, decor->ne, d->pix, gc, 0, 0, visual.corner, visual.corner, fullw - visual.corner, 0);
	XCopyArea(dpy, decor->sw, d->pix, gc, 0, 0, visual.corner, visual.corner, 0, fullh - visual.corner);
	XCopyArea(dpy, decor->se, d->pix, gc, 0, 0, visual.corner, visual.corner, fullw - visual.corner, fullh - visual.corner);

	val.fill_style = FillSolid;
	val.foreground = decor->bg;
	XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
	XFillRectangle(dpy, d->pix, gc, visual.border, visual.border, d->w, d->h);

	XCopyArea(dpy, d->pix, d->frame, gc, 0, 0, fullw, fullh, 0, 0);
}

/* decorate tab */
static void
tabdecorate(struct Tab *t, int pressed)
{
	XGCValues val;
	XRectangle box, dr;
	struct Decor *decor;
	size_t len;
	int style;
	int x, y;

	style = tabgetstyle(t);
	if (t->row != NULL && t != t->row->col->c->selcol->selrow->seltab)
		decor = &visual.decor[style][TAB_UNFOCUSED];
	else if (t->row != NULL && pressed)
		decor = &visual.decor[style][TAB_PRESSED];
	else
		decor = &visual.decor[style][TAB_FOCUSED];

	/* (re)create pixmap */
	if (t->ptw != t->w || t->pixtitle == None) {
		if (t->pixtitle != None)
			XFreePixmap(dpy, t->pixtitle);
		t->pixtitle = XCreatePixmap(dpy, t->title, t->w, visual.tab, depth);
	}
	t->ptw = t->w;

	if (t->pw != t->winw || t->ph != t->winh || t->pix == None) {
		if (t->pix != None)
			XFreePixmap(dpy, t->pix);
		t->pix = XCreatePixmap(dpy, t->frame, t->winw, t->winh, depth);
	}
	t->pw = t->winw;
	t->ph = t->winh;

	/* draw tab */
	val.tile = decor->t;
	val.ts_x_origin = 0;
	val.ts_y_origin = 0;
	val.fill_style = FillTiled;
	XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin | GCFillStyle, &val);
	XFillRectangle(dpy, t->pixtitle, gc, visual.edge, 0, t->w - visual.edge, visual.tab);
	XCopyArea(dpy, decor->tl, t->pixtitle, gc, 0, 0, visual.edge, visual.tab, 0, 0);
	XCopyArea(dpy, decor->tr, t->pixtitle, gc, 0, 0, visual.edge, visual.tab, t->w - visual.edge, 0);

	/* write tab title */
	if (t->name != NULL) {
		len = strlen(t->name);
		val.fill_style = FillSolid;
		val.foreground = decor->fg;
		XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
		XmbTextExtents(visual.fontset, t->name, len, &dr, &box);
		x = (t->w - box.width) / 2 - box.x;
		y = (visual.tab - box.height) / 2 - box.y;
		XmbDrawString(dpy, t->pixtitle, visual.fontset, gc, x, y, t->name, len);
	}

	/* draw frame background */
	if (!pressed) {
		val.foreground = decor->bg;
		val.fill_style = FillSolid;
		XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
		XFillRectangle(dpy, t->pix, gc, 0, 0, t->winw, t->winh);
	}

	XCopyArea(dpy, t->pixtitle, t->title, gc, 0, 0, t->w, visual.tab, 0, 0);
	XCopyArea(dpy, t->pix, t->frame, gc, 0, 0, t->winw, t->winh, 0, 0);
}

/* draw title bar buttons */
static void
buttondecorate(struct Row *row, int button, int pressed)
{
	struct Decor *decor;    /* decoration */
	int style;              /* decoration style, used as index in the decor array */

	style = (row->seltab) ? tabgetstyle(row->seltab) : UNFOCUSED;
	decor = pressed ? &visual.decor[style][PRESSED] : &visual.decor[style][UNPRESSED];

	if (button == BUTTON_LEFT) {
		XCopyArea(dpy, decor->bl, row->pixbl, gc, 0, 0, visual.button, visual.button, 0, 0);
		XCopyArea(dpy, row->pixbl, row->bl, gc, 0, 0, visual.button, visual.button, 0, 0);
	}

	if (button == BUTTON_RIGHT) {
		XCopyArea(dpy, decor->br, row->pixbr, gc, 0, 0, visual.button, visual.button, 0, 0);
		XCopyArea(dpy, row->pixbr, row->br, gc, 0, 0, visual.button, visual.button, 0, 0);
	}
}

/* draw decoration on container frame */
static void
containerdecorate(struct Container *c, struct Column *cdiv, struct Row *rdiv, int recursive, enum Octant o)
{
	struct Decor *decor;    /* unpressed decoration */
	struct Decor *decorp;   /* pressed decoration */
	struct Column *col;
	struct Row *row;
	struct Tab *t;
	struct Dialog *d;
	XGCValues val;
	int style;              /* decoration style, used as index in the decor array */
	int w, h;               /* size of the edges */

	if (c == NULL)
		return;
	style = containergetstyle(c);
	decor = &visual.decor[style][UNPRESSED];
	decorp = &visual.decor[style][PRESSED];
	w = c->w - visual.corner * 2;
	h = c->h - visual.corner * 2;

	/* (re)create pixmap */
	if (c->pw != c->w || c->ph != c->h || c->pix == None) {
		if (c->pix != None)
			XFreePixmap(dpy, c->pix);
		c->pix = XCreatePixmap(dpy, c->frame, c->w, c->h, depth);
	}
	c->pw = c->w;
	c->ph = c->h;

	val.fill_style = FillTiled;
	XChangeGC(dpy, gc, GCFillStyle, &val);
	if (c->b > 0) {
	
		/* draw borders */
		if (w > 0) {
			val.tile = (o == N) ? decorp->n : decor->n;
			val.ts_x_origin = 0;
			val.ts_y_origin = 0;
			XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
			XFillRectangle(dpy, c->pix, gc, visual.corner, 0, w, c->b);
			XCopyArea(dpy, (o == N) ? decorp->nf :
			                decor->nf, c->pix, gc, 0, 0, visual.edge, visual.border,
			                visual.corner, 0);
			XCopyArea(dpy, (o == N) ? decorp->nl :
			                decor->nl, c->pix, gc, 0, 0, visual.edge, visual.border,
			                visual.corner + w - visual.edge, 0);
		}

		if (w > 0) {
			val.tile = (o == S) ? decorp->s : decor->s;
			val.ts_x_origin = 0;
			val.ts_y_origin = c->h - c->b;
			XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin , &val);
			XFillRectangle(dpy, c->pix, gc, visual.corner, c->h - c->b, w, c->b);
			XCopyArea(dpy, (o == S) ? decorp->sf :
			                decor->sf, c->pix, gc, 0, 0, visual.edge, visual.border,
			                visual.corner, c->h - visual.border);
			XCopyArea(dpy, (o == S) ? decorp->sl :
			                decor->sl, c->pix, gc, 0, 0, visual.edge, visual.border,
			                visual.corner + w - visual.edge, c->h - visual.border);
		}

		if (h > 0) {
			val.tile = (o == W) ? decorp->w : decor->w;
			val.ts_x_origin = 0;
			val.ts_y_origin = 0;
			XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin , &val);
			XFillRectangle(dpy, c->pix, gc, 0, visual.corner, c->b, h);
			XCopyArea(dpy, (o == W) ? decorp->wf :
			                decor->wf, c->pix, gc, 0, 0, visual.border, visual.edge, 0,
			                visual.corner);
			XCopyArea(dpy, (o == W) ? decorp->wl :
			                decor->wl, c->pix, gc, 0, 0, visual.border, visual.edge, 0,
			                visual.corner + h - visual.edge);
		}

		if (h > 0) {
			val.tile = (o == E) ? decorp->e : decor->e;
			val.ts_x_origin = c->w - c->b;
			val.ts_y_origin = 0;
			XChangeGC(dpy, gc, GCTile | GCTileStipYOrigin | GCTileStipXOrigin , &val);
			XFillRectangle(dpy, c->pix, gc, c->w - c->b, visual.corner, c->b, h);
			XCopyArea(dpy, (o == E) ? decorp->ef :
			                decor->ef, c->pix, gc, 0, 0, visual.border, visual.edge,
			                c->w - visual.border, visual.corner);
			XCopyArea(dpy, (o == E) ? decorp->el :
			                decor->el, c->pix, gc, 0, 0, visual.border, visual.edge,
			                c->w - visual.border, visual.corner + h - visual.edge);
		}

		XCopyArea(dpy, (o == NW) ? decorp->nw :
		                decor->nw, c->pix, gc, 0, 0, visual.corner, visual.corner, 0, 0);

		XCopyArea(dpy, (o == NE) ? decorp->ne :
		                decor->ne, c->pix, gc, 0, 0, visual.corner, visual.corner,
		                c->w - visual.corner, 0);

		XCopyArea(dpy, (o == SW) ? decorp->sw :
		                decor->sw, c->pix, gc, 0, 0, visual.corner, visual.corner,
		                0, c->h - visual.corner);

		XCopyArea(dpy, (o == SE) ? decorp->se :
		                decor->se, c->pix, gc, 0, 0, visual.corner, visual.corner,
		                c->w - visual.corner, c->h - visual.corner);
	}

	/* draw background */
	if (o == 0) {
		val.foreground = decor->bg;
		val.fill_style = FillSolid;
		XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
		XFillRectangle(dpy, c->pix, gc, c->b, c->b, c->w - 2 * c->b, c->h - 2 * c->b);
	}

	for (col = c->cols; col != NULL; col = col->next) {
		if (col->next != NULL) {
			val.fill_style = FillTiled;
			val.tile = (col == cdiv) ? decorp->e : decor->e;
			val.ts_x_origin = col->x + col->w;
			val.ts_y_origin = 0;
			XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin , &val);
			XFillRectangle(dpy, c->pix, gc, col->x + col->w, c->b, visual.division, c->h - 2 * c->b);
		}
		for (row = col->rows; row != NULL; row = row->next) {
			if (col->maxrow == NULL && row->next != NULL) {
				val.fill_style = FillTiled;
				val.tile = (row == rdiv) ? decorp->s : decor->s;
				val.ts_x_origin = 0;
				val.ts_y_origin = row->y + row->h;
				XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin , &val);
				XFillRectangle(dpy, c->pix, gc, col->x, row->y + row->h, col->w, visual.division);
			}


			/* (re)create titlebar pixmap */
			if (row->pw != col->w || row->pixbar == None) {
				if (row->pixbar != None)
					XFreePixmap(dpy, row->pixbar);
				row->pixbar = XCreatePixmap(dpy, row->bar, col->w, visual.tab, depth);
			}
			row->pw = col->w;

			val.foreground = decor->bg;
			val.fill_style = FillSolid;
			XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
			XFillRectangle(dpy, row->pixbar, gc, 0, 0, c->w, visual.tab);
			XCopyArea(dpy, row->pixbar, row->bar, gc, 0, 0, col->w, visual.tab, 0, 0);

			buttondecorate(row, BUTTON_LEFT, 0);
			buttondecorate(row, BUTTON_RIGHT, 0);
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
	dx = d->x - visual.border;
	dy = d->y - visual.border;
	dw = d->w + 2 * visual.border;
	dh = d->h + 2 * visual.border;
	XMoveResizeWindow(dpy, d->frame, dx, dy, dw, dh);
	XMoveResizeWindow(dpy, d->win, visual.border, visual.border, d->w, d->h);
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
	XMoveResizeWindow(dpy, t->title, t->x, 0, t->w, visual.tab);
	if (t->ptw != t->w) {
		tabdecorate(t, 0);
	}
}

/* commit titlebar size and position */
static void
titlebarmoveresize(struct Row *row, int x, int y, int w)
{
	XMoveResizeWindow(dpy, row->bar, x, y, w, visual.tab);
	XMoveWindow(dpy, row->bl, 0, 0);
	XMoveWindow(dpy, row->br, w - visual.button, 0);
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

	if (c == NULL)
		return;
	XMoveResizeWindow(dpy, c->frame, c->x, c->y, c->w, c->h);
	XMoveResizeWindow(dpy, c->curswin, 0, 0, c->w, c->h);
	for (col = c->cols; col != NULL; col = col->next) {
		rowy = c->b;
		rowh = max(1, c->h - 2 * c->b - col->nrows * visual.tab);
		for (row = col->rows; row != NULL; row = row->next) {
			if (col->maxrow == NULL) {              /* regular row */
				titlebarmoveresize(row, col->x, row->y, col->w);
				XMoveResizeWindow(dpy, row->frame, col->x, row->y + visual.tab, col->w, row->h - visual.tab);
				XMapWindow(dpy, row->frame);
			} else if (row == col->maxrow) {        /* maximized row */
				titlebarmoveresize(row, col->x, rowy, col->w);
				XMoveResizeWindow(dpy, row->frame, col->x, rowy + visual.tab, col->w, rowh);
				XMapWindow(dpy, row->frame);
				rowy += rowh;
			} else {                                /* minimized row */
				titlebarmoveresize(row, col->x, rowy, col->w);
				XUnmapWindow(dpy, row->frame);
			}
			rowy += visual.tab;
			for (t = row->tabs; t != NULL; t = t->next) {
				XMoveResizeWindow(dpy, t->frame, 0, 0, t->winw, t->winh);
				for (d = t->ds; d != NULL; d = d->next) {
					dialogmoveresize(d);
				}
				XResizeWindow(dpy, t->win, t->winw, t->winh);
				winnotify(t->win, c->x + col->x, c->y + row->y + visual.tab, t->winw, t->winh);
				ewmhsetframeextents(t->win, c->b, (c->isfullscreen && c->ncols == 1 && c->cols->nrows == 1) ? 0 : visual.tab);
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
	if ((valuemask & CWWidth) && wc->width >= visual.center + 2 * visual.border)
		c->nw = wc->width;
	if ((valuemask & CWHeight) && wc->height >= visual.center + 2 * visual.border)
		c->nh = wc->height;
	containercalccols(c, 1);
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
	XRestackWindows(dpy, wins, sizeof(wins));
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
	containercalccols(c, 1);
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
	containercalccols(c, 1);
	containermoveresize(c);
	containerredecorate(c, NULL, NULL, 0);
	ewmhsetstate(c);
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
	c->ishidden = 0;
	c->layer = 0;
	c->desk = NULL;
	c->pw = c->ph = 0;
	c->x = c->nx = x - visual.border;
	c->y = c->ny = y - visual.border;
	c->w = c->nw = w + 2 * visual.border;
	c->h = c->nh = h + 2 * visual.border + visual.tab;
	c->b = visual.border;
	c->pix = None;
	c->frame = XCreateWindow(dpy, root, c->x, c->y, c->w, c->h, 0,
	                         CopyFromParent, CopyFromParent, CopyFromParent,
	                         CWEventMask, &clientswa);
	c->curswin = XCreateWindow(dpy, c->frame, 0, 0, c->w, c->h, 0,
	                           CopyFromParent, InputOnly, CopyFromParent,
	                           0, NULL);
	XMapWindow(dpy, c->curswin);
	if (wm.c)
		wm.c->prev = c;
	c->next = wm.c;
	wm.c = c;
	containeraddfocus(c);
	return c;
}

/* move container x pixels to the right and y pixels down */
static void
containerincrmove(struct Container *c, int x, int y)
{
	struct Monitor *monto;

	if (c == NULL || c->isminimized || c->ismaximized || c->isfullscreen)
		return;
	c->nx += x;
	c->ny += y;
	c->x = c->nx;
	c->y = c->ny;
	containermoveresize(c);
	if (!c->issticky) {
		monto = getmon(c->nx + c->nw / 2, c->ny + c->nh / 2);
		if (monto != NULL && monto != c->mon) {
			containersendtodesk(c, monto->seldesk, 0, 0);
		}
	}
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
		colcalcrows(row->col, 0);
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
	containercalccols(col->c, 0);
}

/* delete column */
static void
coldel(struct Column *col)
{
	while (col->rows)
		rowdel(col->rows);
	coldetach(col);
	free(col);
}

/* delete container */
static void
containerdel(struct Container *c)
{
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
	XDestroyWindow(dpy, c->curswin);
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
	containercalccols(c, 0);
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
	colcalcrows(col, 0);    /* set row->y, row->h, etc */
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
	row->bl = XCreateWindow(dpy, row->bar, 0, 0, visual.button, visual.button, 0,
	                        CopyFromParent, CopyFromParent, CopyFromParent,
	                        CWEventMask, &clientswa);
	row->pixbl = XCreatePixmap(dpy, row->bl, visual.button, visual.button, depth);
	row->br = XCreateWindow(dpy, row->bar, 0, 0, visual.button, visual.button, 0,
	                        CopyFromParent, CopyFromParent, CopyFromParent,
	                        CWEventMask, &clientswa);
	row->pixbr = XCreatePixmap(dpy, row->bl, visual.button, visual.button, depth);
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
		t->title = XCreateWindow(dpy, row->bar, t->x, 0, t->w, visual.tab, 0,
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
deskfocus(struct Desktop *desk)
{
	void tabfocus(struct Tab *t, int gotodesk);
	struct Container *c;
	int cursorx, cursory;
	Window da, db;          /* dummy variables */
	int dx, dy;             /* dummy variables */
	unsigned int du;        /* dummy variable */

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

	/* if changing focus to a new monitor and the cursor isn't there, warp it */
	XQueryPointer(dpy, root, &da, &db, &cursorx, &cursory, &dx, &dy, &du);
	if (desk->mon != wm.selmon && desk->mon != getmon(cursorx, cursory)) {
		XWarpPointer(dpy, None, root, 0, 0, 0, 0, desk->mon->mx + desk->mon->mw / 2,
		                                         desk->mon->my + desk->mon->mh / 2);
	}

	/* update current desktop */
	wm.selmon = desk->mon;
	wm.selmon->seldesk = desk;
	if (wm.showingdesk)
		deskshow(0);
	ewmhsetcurrentdesktop(desk->n);

	/* focus client on the new current desktop */
	c = getnextfocused(desk->mon, desk);
	if (c != NULL) {
		tabfocus(c->selcol->selrow->seltab, 0);
	} else {
		tabfocus(NULL, 0);
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
		if (t->ds != NULL) {
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
			deskfocus(c->issticky ? c->mon->seldesk : c->desk);
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
					colcalcrows(col, 1);
					goto done;
				}
				rowy = c->b;
				for (row = col->rows; row != NULL; row = row->next) {
					if (col->maxrow != NULL) {
						if (row == col->maxrow) {
							rowh = c->h - 2 * c->b - (col->nrows - 1) * visual.tab;
						} else {
							rowh = visual.tab;
						}
					} else {
						rowh = row->h;
					}
					if (yroot - c->y >= rowy &&
					    yroot - c->y < rowy + visual.tab) {
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
					    yroot - c->y < rowy + rowh + visual.division) {
						nrow = rownew();
						coladdrow(col, nrow, row);
						rowaddtab(nrow, det, NULL);
						colcalcrows(col, 1);
						goto done;
					}
					rowy += rowh + visual.division;
				}
			}
			if (xroot - c->x >= col->x + col->w - DROPPIXELS &&
			    xroot - c->x < col->x + col->w + visual.division + DROPPIXELS) {
				nrow = rownew();
				ncol = colnew();
				containeraddcol(c, ncol, col);
				coladdrow(ncol, nrow, NULL);
				rowaddtab(nrow, det, NULL);
				containercalccols(c, 1);
				goto done;
			}
		}
		if (xroot - c->x < c->b + DROPPIXELS) {
			nrow = rownew();
			ncol = colnew();
			containeraddcol(c, ncol, NULL);
			coladdrow(ncol, nrow, NULL);
			rowaddtab(nrow, det, NULL);
			containercalccols(c, 1);
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
	struct Dock *dock;
	struct Container *c;
	int t, b, l, r;

	for (mon = wm.monhead; mon != NULL; mon = mon->next) {
		mon->wx = mon->mx;
		mon->wy = mon->my;
		mon->ww = mon->mw;
		mon->wh = mon->mh;
		t = b = l = r = 0;
		for (dock = wm.docks; dock != NULL; dock = dock->next) {
			if (dock->strut[STRUT_TOP] != 0) {
				if (dock->strut[STRUT_TOP] >= mon->my &&
				    dock->strut[STRUT_TOP] < mon->my + mon->mh &&
				    (!dock->partial ||
				     (dock->strut[STRUT_TOP_START_X] >= mon->mx &&
				     dock->strut[STRUT_TOP_END_X] <= mon->mx + mon->mw))) {
					t = max(t, dock->strut[STRUT_TOP] - mon->my);
				}
			} else if (dock->strut[STRUT_BOTTOM] != 0) {
				if (screenh - dock->strut[STRUT_BOTTOM] <= mon->my + mon->mh &&
				    screenh - dock->strut[STRUT_BOTTOM] > mon->my &&
				    (!dock->partial ||
				     (dock->strut[STRUT_BOTTOM_START_X] >= mon->mx &&
				     dock->strut[STRUT_BOTTOM_END_X] <= mon->mx + mon->mw))) {
					b = max(b, dock->strut[STRUT_BOTTOM] - (screenh - (mon->my + mon->mh)));
				}
			} else if (dock->strut[STRUT_LEFT] != 0) {
				if (dock->strut[STRUT_LEFT] >= mon->mx &&
				    dock->strut[STRUT_LEFT] < mon->mx + mon->mw &&
				    (!dock->partial ||
				     (dock->strut[STRUT_LEFT_START_Y] >= mon->my &&
				     dock->strut[STRUT_LEFT_END_Y] <= mon->my + mon->mh))) {
					l = max(l, dock->strut[STRUT_LEFT] - mon->mx);
				}
			} else if (dock->strut[STRUT_RIGHT] != 0) {
				if (screenw - dock->strut[STRUT_RIGHT] <= mon->mx + mon->mw &&
				    screenw - dock->strut[STRUT_RIGHT] > mon->mx &&
				    (!dock->partial ||
				     (dock->strut[STRUT_RIGHT_START_Y] >= mon->my &&
				     dock->strut[STRUT_RIGHT_END_Y] <= mon->my + mon->mh))) {
					r = max(r, dock->strut[STRUT_RIGHT] - (screenw - (mon->mx + mon->mw)));
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
			containercalccols(c, 1);
			containermoveresize(c);
			containerredecorate(c, NULL, NULL, 0);
		}
	}
}

/* select window input events, grab mouse button presses, and clear its border */
static void
preparewin(Window win)
{
	XSelectInput(dpy, win, EnterWindowMask | StructureNotifyMask
	                     | PropertyChangeMask | FocusChangeMask);
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
	*w = min(*w, wm.selmon->ww - visual.border * 2);
	*h = min(*h, wm.selmon->wh - visual.border);
	*x = wm.selmon->wx + (wm.selmon->ww - *w) / 2 - visual.border;
	*y = 0;
	*fw = *w + visual.border * 2;
	*fh = *h + visual.border;
}

/* decorate prompt frame */
static void
promptdecorate(Window frame, int w, int h)
{
	XGCValues val;

	val.fill_style = FillSolid;
	val.foreground = visual.decor[FOCUSED][2].bg;
	XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
	XFillRectangle(dpy, frame, gc, visual.border, visual.border, w, h);

	val.fill_style = FillTiled;
	val.tile = visual.decor[FOCUSED][2].w;
	val.ts_x_origin = 0;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, frame, gc, 0, 0, visual.border, h + visual.border);

	val.fill_style = FillTiled;
	val.tile = visual.decor[FOCUSED][2].e;
	val.ts_x_origin = visual.border + w;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, frame, gc, visual.border + w, 0, visual.border, h + visual.border);

	val.fill_style = FillTiled;
	val.tile = visual.decor[FOCUSED][2].s;
	val.ts_x_origin = 0;
	val.ts_y_origin = visual.border + h;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, frame, gc, visual.border, h, w + 2 * visual.border, visual.border);

	XCopyArea(dpy, visual.decor[FOCUSED][2].sw, frame, gc, 0, 0, visual.corner, visual.corner, 0, h + visual.border - visual.corner);
	XCopyArea(dpy, visual.decor[FOCUSED][2].se, frame, gc, 0, 0, visual.corner, visual.corner, w + 2 * visual.border - visual.corner, h + visual.border - visual.corner);
}

/* create notification window */
static void
notifnew(Window win, int w, int h)
{
	struct Notification *n;

	n = emalloc(sizeof(*n));
	n->w = w + 2 * visual.border;
	n->h = h + 2 * visual.border;
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

/* decorate notification */
static void
notifdecorate(struct Notification *n, int style)
{
	XGCValues val;
	int w, h;

	if (n->pw != n->w || n->ph != n->h || n->pix == None) {
		if (n->pix != None)
			XFreePixmap(dpy, n->pix);
		n->pix = XCreatePixmap(dpy, n->frame, n->w, n->h, depth);
	}
	n->pw = n->w;
	n->ph = n->h;

	w = n->w - 2 * visual.border;
	h = n->h - 2 * visual.border;

	val.fill_style = FillTiled;
	val.tile = visual.decor[style][NOTIFICATION].w;
	val.ts_x_origin = 0;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, n->pix, gc, 0, visual.border, visual.border, h);

	val.tile = visual.decor[style][NOTIFICATION].e;
	val.ts_x_origin = visual.border + w;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, n->pix, gc, visual.border + w, visual.border, visual.border, h);

	val.tile = visual.decor[style][NOTIFICATION].n;
	val.ts_x_origin = 0;
	val.ts_y_origin = 0;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, n->pix, gc, visual.border, 0, w, visual.border);

	val.tile = visual.decor[style][NOTIFICATION].s;
	val.ts_x_origin = 0;
	val.ts_y_origin = visual.border + h;
	XChangeGC(dpy, gc, GCFillStyle | GCTile | GCTileStipYOrigin | GCTileStipXOrigin, &val);
	XFillRectangle(dpy, n->pix, gc, visual.border, visual.border + h, w, visual.border);

	XCopyArea(dpy, visual.decor[style][NOTIFICATION].nw, n->pix, gc, 0, 0, visual.corner, visual.corner, 0, 0);
	XCopyArea(dpy, visual.decor[style][NOTIFICATION].ne, n->pix, gc, 0, 0, visual.corner, visual.corner, n->w - visual.corner, 0);
	XCopyArea(dpy, visual.decor[style][NOTIFICATION].sw, n->pix, gc, 0, 0, visual.corner, visual.corner, 0, n->h - visual.corner);
	XCopyArea(dpy, visual.decor[style][NOTIFICATION].se, n->pix, gc, 0, 0, visual.corner, visual.corner, n->w - visual.corner, n->h - visual.corner);

	val.fill_style = FillSolid;
	val.foreground = visual.decor[style][NOTIFICATION].bg;
	XChangeGC(dpy, gc, GCFillStyle | GCForeground, &val);
	XFillRectangle(dpy, n->pix, gc, visual.border, visual.border, w, h);

	XCopyArea(dpy, n->pix, n->frame, gc, 0, 0, n->w, n->h, 0, 0);
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
		switch (config.gravity) {
		case NorthWestGravity:
			break;
		case NorthGravity:
			x += (wm.monhead->ww - n->w) / 2;
			break;
		case NorthEastGravity:
			x += wm.monhead->ww - n->w;
			break;
		case WestGravity:
			y += (wm.monhead->wh - n->h) / 2;
			break;
		case CenterGravity:
			x += (wm.monhead->ww - n->w) / 2;
			y += (wm.monhead->wh - n->h) / 2;
			break;
		case EastGravity:
			x += wm.monhead->ww - n->w;
			y += (wm.monhead->wh - n->h) / 2;
			break;
		case SouthWestGravity:
			y += wm.monhead->wh - n->h;
			break;
		case SouthGravity:
			x += (wm.monhead->ww - n->w) / 2;
			y += wm.monhead->wh - n->h;
			break;
		case SouthEastGravity:
			x += wm.monhead->ww - n->w;
			y += wm.monhead->wh - n->h;
			break;
		}

		if (config.direction == DOWNWARDS)
			y += h;
		else
			y -= h;
		h += n->h + config.notifgap + visual.border * 2;

		XMoveResizeWindow(dpy, n->frame, x, y, n->w, n->h);
		XMoveResizeWindow(dpy, n->win, visual.border, visual.border, n->w - 2 * visual.border, n->h - 2 * visual.border);
		XMapWindow(dpy, n->frame);
		winnotify(n->win, x + visual.border, y + visual.border, n->w - 2 * visual.border, n->h - 2 * visual.border);
		if (n->pw != n->w || n->ph != n->h) {
			notifdecorate(n, FOCUSED);
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

/* fill strut array of dock */
static void
dockstrut(struct Dock *dock)
{
	unsigned long *arr;
	unsigned long l, i;

	for (i = 0; i < STRUT_LAST; i++)
		dock->strut[i] = 0;
	dock->partial = 1;
	l = getcardprop(dock->win, atoms[_NET_WM_STRUT_PARTIAL], &arr);
	if (arr == NULL) {
		dock->partial = 0;
		l = getcardprop(dock->win, atoms[_NET_WM_STRUT], &arr);
		if (arr == NULL) {
			return;
		}
	}
	for (i = 0; i < STRUT_LAST && i < l; i++)
		dock->strut[i] = arr[i];
	XFree(arr);
}

/* delete dock */
static void
dockdel(struct Dock *dock)
{
	if (dock->next != NULL)
		dock->next->prev = dock->prev;
	if (dock->prev != NULL)
		dock->prev->next = dock->next;
	else
		wm.docks = dock->next;
	free(dock);
	monupdatearea();
}

/* call the proper decorate function */
static void
decorate(struct Winres *res)
{
	int fullw, fullh;

	if (res->n) {
		XCopyArea(dpy, res->n->pix, res->n->frame, gc, 0, 0, res->n->w, res->n->h, 0, 0);
	} else if (res->d != NULL) {
		fullw = res->d->w + 2 * visual.border;
		fullh = res->d->h + 2 * visual.border;
		XCopyArea(dpy, res->d->pix, res->d->frame, gc, 0, 0, fullw, fullh, 0, 0);
	} else if (res->t != NULL) {
		XCopyArea(dpy, res->t->pixtitle, res->t->title, gc, 0, 0, res->t->w, visual.tab, 0, 0);
		XCopyArea(dpy, res->t->pix, res->t->frame, gc, 0, 0, res->t->winw, res->t->winh, 0, 0);
	} else if (res->row != NULL) {
		XCopyArea(dpy, res->row->pixbar, res->row->bar, gc, 0, 0, res->row->pw, visual.tab, 0, 0);
		XCopyArea(dpy, res->row->pixbl, res->row->bl, gc, 0, 0, visual.button, visual.button, 0, 0);
		XCopyArea(dpy, res->row->pixbr, res->row->br, gc, 0, 0, visual.button, visual.button, 0, 0);
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
	XReparentWindow(dpy, win, prompt.frame, visual.border, 0);
	XMapWindow(dpy, win);
	XMapWindow(dpy, prompt.frame);
	XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
	prompt.win = win;
	while (!XIfEvent(dpy, &ev, promptvalidevent, (XPointer)&prompt)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				if (ev.xexpose.window == prompt.frame) {
					promptdecorate(prompt.frame, w, h);
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
			XMoveResizeWindow(dpy, win, visual.border, 0, w, h);
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

	XRestackWindows(dpy, wins, sizeof wins);
	XMapWindow(dpy, win);
}

/* map dockapp window */
static void
managedockapp(Window win)
{
	Window wins[2] = {wm.layerwins[LAYER_DESKTOP], win};

	XRestackWindows(dpy, wins, sizeof wins);
	XMapWindow(dpy, win);
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

/* map dock window */
static void
managedock(Window win)
{
	struct Dock *dock;
	Window wins[2] = {wm.layerwins[LAYER_DOCK], win};

	dock = emalloc(sizeof(*dock));
	dock->prev = dock->next = NULL;
	dock->win = win;
	if (wm.docks != NULL)
		wm.docks->prev = dock;
	dock->next = wm.docks;
	wm.docks = dock;
	XRestackWindows(dpy, wins, sizeof wins);
	XMapWindow(dpy, win);
	dockstrut(dock);
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
	if (res.c != NULL || res.dock != NULL)
		return;
	switch (getwintype(win)) {
	case TYPE_DESKTOP:
		managedesktop(win);
		break;
	case TYPE_DOCKAPP:
		managedockapp(win);
		break;
	case TYPE_DOCK:
		managedock(win);
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
		containercalccols(c, 1);
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
	tabdetach(t, xroot - x, yroot - y, c->nw - 2 * visual.border, c->nh - 2 * visual.border - visual.tab);
	containermoveresize(c);
	XGrabPointer(dpy, t->title, False,
	             ButtonReleaseMask | PointerMotionMask,
	             GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	while (!XMaskEvent(dpy, ButtonReleaseMask | PointerMotionMask | ExposureMask, &ev)) {
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0) {
				if (ev.xexpose.window == t->title) {
					XCopyArea(dpy, t->pixtitle, t->title, gc, 0, 0, t->w, visual.tab, 0, 0);
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
		newc = containernew(xroot - x - visual.button, yroot - y, t->winw, t->winh);
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
		containercalccols(c, 1);
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
				if (c->nw + dx >= visual.center + 2 * c->b) {
					c->nx -= dx;
					c->nw += dx;
				}
			} else if (o & E &&
			    ((ev.xmotion.x_root > xroot && x > c->nx + c->nw - ev.xmotion.x_root) ||
			     (ev.xmotion.x_root < xroot && x < c->nx + c->nw - ev.xmotion.x_root))) {
				dx = ev.xmotion.x_root - xroot;
				if (c->nw + dx >= visual.center + 2 * c->b) {
					c->nw += dx;
				}
			}
			if (o & N &&
			    ((ev.xmotion.y_root < yroot && y > ev.xmotion.y_root - c->ny) ||
			     (ev.xmotion.y_root > yroot && y < ev.xmotion.y_root - c->ny))) {
				dy = yroot - ev.xmotion.y_root;
				if (c->nh + dy >= visual.center + 2 * c->b) {
					c->ny -= dy;
					c->nh += dy;
				}
			} else if (o & S &&
			    ((ev.xmotion.y_root > yroot && c->ny + c->nh - ev.xmotion.y_root < y) ||
			     (ev.xmotion.y_root < yroot && c->ny + c->nh - ev.xmotion.y_root > y))) {
				dy = ev.xmotion.y_root - yroot;
				if (c->nh + dy >= visual.center + 2 * c->b) {
					c->nh += dy;
				}
			}
			if (ev.xmotion.time - lasttime > RESIZETIME) {
				containercalccols(c, 1);
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
	containercalccols(c, 1);
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
	buttondecorate(row, BUTTON_LEFT, 1);
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
				    ev.xmotion.y_root < c->y + c->h - c->b - visual.tab) {
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
	containercalccols(c, 1);
	containermoveresize(c);
	buttondecorate(row, BUTTON_LEFT, 0);
	containerdecorate(c, NULL, NULL, 0, 0);
	XUngrabPointer(dpy, CurrentTime);
}

/* close tab with mouse */
static void
mouseclose(struct Row *row)
{
	struct Winres res;
	XEvent ev;

	buttondecorate(row, BUTTON_RIGHT, 1);
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
			    ev.xbutton.x < visual.button && ev.xbutton.x < visual.button)
				winclose(row->seltab->ds != NULL ? row->seltab->ds->win : row->seltab->win);
			goto done;
			break;
		}
	}
done:
	buttondecorate(row, BUTTON_RIGHT, 0);
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
				if (x < 0 && cdiv->w + x > visual.center) {
					cdiv->w += x;
					cdiv->next->w -= x;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				} else if (x > 0 && cdiv->next->w - x > visual.center) {
					cdiv->next->w -= x;
					cdiv->w += x;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
			} else if (rdiv != NULL) {
				if (y < 0 && rdiv->h + y > visual.center) {
					rdiv->h += y;
					rdiv->next->h -= y;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				} else if (y > 0 && rdiv->next->h - y > visual.center) {
					rdiv->next->h -= y;
					rdiv->h += y;
					if (ev.xmotion.time - lasttime > RESIZETIME) {
						update = 1;
					}
				}
			}
			if (update) {
				containercalccols(c, 1);
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
	containercalccols(c, 1);
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

	buttondecorate(row, BUTTON_LEFT, 1);
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
			    ev.xbutton.x < visual.button && ev.xbutton.x < visual.button) {
				rowstack(row->col, (row->col->maxrow == row) ? NULL : row);
				tabfocus(row->seltab, 0);
			}
			goto done;
			break;
		}
	}
done:
	buttondecorate(row, BUTTON_LEFT, 0);
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
			deskfocus(mon->seldesk);
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
	if ((wm.focused == NULL || t != wm.focused->selcol->selrow->seltab) &&
	    ((ev->window == t->title && ev->button == Button1) ||
	     (ev->button == Button1 && config.focusbuttons & 1 << 0) ||
	     (ev->button == Button2 && config.focusbuttons & 1 << 1) ||
	     (ev->button == Button3 && config.focusbuttons & 1 << 2) ||
	     (ev->button == Button4 && config.focusbuttons & 1 << 3) ||
	     (ev->button == Button5 && config.focusbuttons & 1 << 4)))
		tabfocus(t, 1);

	/* raise client */
	if ((c != wm.abovelist || c != wm.centerlist || c != wm.belowlist) &&
	    ((ev->button == Button1 && config.raisebuttons & 1 << 0) ||
	     (ev->button == Button2 && config.raisebuttons & 1 << 1) ||
	     (ev->button == Button3 && config.raisebuttons & 1 << 2) ||
	     (ev->button == Button4 && config.raisebuttons & 1 << 3) ||
	     (ev->button == Button5 && config.raisebuttons & 1 << 4)))
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
		if (ev->state == config.modifier && ev->button == Button1) {
			mousemove(c, ev->x_root, ev->y_root, 0);
		} else if (ev->window == c->frame && ev->button == Button3) {
			mousemove(c, ev->x_root, ev->y_root, o);
		} else if ((ev->state == config.modifier && ev->button == Button3) ||
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
		deskfocus(getdesk(ev->data.l[0], ev->data.l[2]));
	} else if (ev->message_type == atoms[_NET_SHOWING_DESKTOP]) {
		if (ev->data.l[0]) {
			deskshow(1);
		} else {
			deskfocus(wm.selmon->seldesk);
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
		wc.x = (ev->data.l[0] & _SHOD_RELATIVE_X) ? res.c->x + res.c->b + ev->data.l[1] : ev->data.l[1];
		wc.y = (ev->data.l[0] & _SHOD_RELATIVE_Y) ? res.c->y + res.c->b + ev->data.l[2] : ev->data.l[2];
		wc.width = (ev->data.l[0] & _SHOD_RELATIVE_WIDTH) ? res.c->w + ev->data.l[3] - 2 * res.c->b : ev->data.l[3];
		wc.height = (ev->data.l[0] & _SHOD_RELATIVE_HEIGHT) ? res.c->h + ev->data.l[4] - 2 * res.c->b : ev->data.l[4];
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
			ewmhsetframeextents(ev->window, visual.border, visual.tab);
		} else {
			ewmhsetframeextents(ev->window, res.c->b, (res.c->isfullscreen && res.c->ncols == 1 && res.c->cols->nrows == 1) ? 0 : visual.tab);
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
		containerconfigure(res.c, ev->value_mask, &wc);
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
	if (res.n && ev->window == res.n->win) {
		notifdel(res.n);
		return;
	} else if (res.dock != NULL && ev->window == res.dock->win) {
		dockdel(res.dock);
		return;
	} else if (res.d && ev->window == res.d->win) {
		dialogdel(res.d);
	} else if (res.t && ev->window == res.t->win) {
		unmanage(res.t);
	}
	ewmhsetclients();
	ewmhsetclientsstacking();
}

/* focus window when cursor enter it (only if there is no focus button) */
static void
xevententernotify(XEvent *e)
{
	struct Winres res;

	if (config.focusbuttons)
		return;
	while (XCheckTypedEvent(dpy, EnterNotify, e))
		;
	res = getwin(e->xcrossing.window);
	if (res.t != NULL) {
		tabfocus(res.t, 1);
	}
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

/* change mouse cursor */
static void
xeventmotionnotify(XEvent *e)
{
	XMotionEvent *ev;
	struct Container *c;
	struct Column *cdiv;
	struct Row *rdiv;
	struct Winres res;
	enum Octant o;

	ev = &e->xmotion;
	res = getwin(ev->window);
	if (res.c == NULL || ev->subwindow != res.c->curswin)
		return;
	c = res.c;
	o = getoctant(c, ev->x, ev->y);
	switch (o) {
	case NW:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_NW]);
		break;
	case NE:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_NE]);
		break;
	case SW:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_SW]);
		break;
	case SE:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_SE]);
		break;
	case N:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_N]);
		break;
	case S:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_S]);
		break;
	case W:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_W]);
		break;
	case E:
		XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_E]);
		break;
	default:
		getdivisions(c, &cdiv, &rdiv, ev->x, ev->y);
		if (cdiv != NULL)
			XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_H]);
		else if (rdiv != NULL)
			XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_V]);
		else
			XDefineCursor(dpy, c->curswin, visual.cursors[CURSOR_NORMAL]);
		break;
	}
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
	} else if (res.dock != NULL && (ev->atom == _NET_WM_STRUT_PARTIAL || ev->atom == _NET_WM_STRUT)) {
		dockstrut(res.dock);
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
	if (res.n && ev->window == res.n->win) {
		notifdel(res.n);
		return;
	} else if (res.dock != NULL && ev->window == res.dock->win) {
		dockdel(res.dock);
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
	while (wm.docks != NULL) {
		dockdel(wm.docks);
	}
	while (wm.monhead != NULL) {
		mondel(wm.monhead);
	}
}

/* free pixmaps */
static void
cleanpixmaps(void)
{
	int i, j;

	for (i = 0; i < STYLE_LAST; i++) {
		for (j = 0; i < DECOR_LAST; i++) {
			XFreePixmap(dpy, visual.decor[i][j].bl);
			XFreePixmap(dpy, visual.decor[i][j].br);
			XFreePixmap(dpy, visual.decor[i][j].tl);
			XFreePixmap(dpy, visual.decor[i][j].t);
			XFreePixmap(dpy, visual.decor[i][j].tr);
			XFreePixmap(dpy, visual.decor[i][j].nw);
			XFreePixmap(dpy, visual.decor[i][j].nf);
			XFreePixmap(dpy, visual.decor[i][j].n);
			XFreePixmap(dpy, visual.decor[i][j].nl);
			XFreePixmap(dpy, visual.decor[i][j].ne);
			XFreePixmap(dpy, visual.decor[i][j].wf);
			XFreePixmap(dpy, visual.decor[i][j].w);
			XFreePixmap(dpy, visual.decor[i][j].wl);
			XFreePixmap(dpy, visual.decor[i][j].ef);
			XFreePixmap(dpy, visual.decor[i][j].e);
			XFreePixmap(dpy, visual.decor[i][j].el);
			XFreePixmap(dpy, visual.decor[i][j].sw);
			XFreePixmap(dpy, visual.decor[i][j].sf);
			XFreePixmap(dpy, visual.decor[i][j].s);
			XFreePixmap(dpy, visual.decor[i][j].sl);
			XFreePixmap(dpy, visual.decor[i][j].se);
		}
	}
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
		[EnterNotify]      = xevententernotify,
		[Expose]           = xeventexpose,
		[FocusIn]          = xeventfocusin,
		[MapRequest]       = xeventmaprequest,
		[MotionNotify]     = xeventmotionnotify,
		[PropertyNotify]   = xeventpropertynotify,
		[UnmapNotify]      = xeventunmapnotify
	};

	/* parse command-line options */
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
	gc = XCreateGC(dpy, root, 0, NULL);
	xerrorxlib = XSetErrorHandler(xerror);

	/* initialize */
	initsignal();
	initdummywindows();
	initfontset();
	initcursors();
	initatoms();
	initnotif();
	initroot();
	inittheme();

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
	cleanpixmaps();
	cleanfontset();
	cleanwm();

	/* clear ewmh hints */
	ewmhsetclients();
	ewmhsetclientsstacking();
	ewmhsetactivewindow(None);

	/* close connection to server */
	XUngrabPointer(dpy, CurrentTime);
	XCloseDisplay(dpy);

	return 0;
}
