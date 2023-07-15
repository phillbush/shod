#include <sys/queue.h>

#include "xutil.h"

#define SHELL                   "SHELL"
#define DEF_SHELL               "sh"
#define DNDDIFF                 10      /* pixels from pointer to place dnd marker */
#define IGNOREUNMAP             6       /* number of unmap notifies to ignore while scanning existing clients */
#define NAMEMAXLEN              256     /* maximum length of window's name */
#define DROPPIXELS              30      /* number of pixels from the border where a tab can be dropped in */
#define DOCKBORDER              1
#define LEN(x)                  (sizeof(x) / sizeof((x)[0]))
#define _SHOD_MOVERESIZE_RELATIVE       ((long)(1 << 16))
#define ISDUMMY(c)              ((c)->ncols == 0)

#define TITLEWIDTH(c)   ((c)->isfullscreen ? 0 : config.titlewidth)

#define TAB_FOREACH_BEGIN(c, tab) {                             \
	struct Column *col;                                     \
	struct Row *row;                                        \
	TAILQ_FOREACH(col, &(c)->colq, entry) {                 \
		TAILQ_FOREACH(row, &col->rowq, entry) {         \
			TAILQ_FOREACH(tab, &row->tabq, entry)
#define TAB_FOREACH_END }                                       \
		}                                               \
	}

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

enum {
	/* border array indices */
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

enum {
	/* cursor array indices */
	CURSOR_NORMAL,                          /* regular cursor */
	CURSOR_MOVE,                            /* arrow-cross cursor */
	CURSOR_NW,                              /* north-west pointing cursor */
	CURSOR_NE,                              /* north-east pointing cursor */
	CURSOR_SW,                              /* south-west pointing cursor */
	CURSOR_SE,                              /* south-east pointing cursor */
	CURSOR_N,                               /* north pointing cursor */
	CURSOR_S,                               /* south pointing cursor */
	CURSOR_W,                               /* west pointing cursor */
	CURSOR_E,                               /* east pointing cursor */
	CURSOR_V,                               /* vertical arrow cursor */
	CURSOR_H,                               /* horizontal arrow cursor */
	CURSOR_HAND,                            /* hand cursor */
	CURSOR_PIRATE,                          /* pirate-cross cursor */
	CURSOR_LAST
};

enum {
	/* application window array indices */
	TYPE_UNKNOWN,
	TYPE_NORMAL,
	TYPE_DESKTOP,
	TYPE_DOCK,
	TYPE_MENU,
	TYPE_DIALOG,
	TYPE_NOTIFICATION,
	TYPE_PROMPT,
	TYPE_SPLASH,
	TYPE_DOCKAPP,
	TYPE_POPUP,
	TYPE_LAST
};

enum {
	/* color array indices */
	COLOR_BG     = 0,
	COLOR_BORD   = 1,
	COLOR_FG     = 2,

	COLOR_MID    = 0,
	COLOR_LIGHT  = 1,
	COLOR_DARK   = 2,
	COLOR_LAST   = 3
};

enum {
	/* decoration style array indices */
	FOCUSED,
	UNFOCUSED,
	URGENT,
	STYLE_OTHER,
	STYLE_LAST
};

enum {
	/* window layer array indices */
	LAYER_BELOW,
	LAYER_NORMAL,
	LAYER_ABOVE,
	LAYER_MENU,
	LAYER_DOCK,
	LAYER_FULLSCREEN,
	LAYER_LAST
};

enum {
	/* strut elements array indices */
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

enum {
	/* container states bits*/
	ABOVE           = 0x01,
	BELOW           = 0x02,
	FULLSCREEN      = 0x04,
	MAXIMIZED       = 0x08,
	MINIMIZED       = 0x10,
	SHADED          = 0x20,
	STICKY          = 0x40,
	USERPLACED      = 0x80,

	/* dockapp states bits */
	EXTEND          = 0x100,
	SHRUNK          = 0x200,
	RESIZED         = 0x400,
};

enum {
	/* container state action */
	REMOVE = 0,
	ADD    = 1,
	TOGGLE = 2
};

enum Octant {
	/* window eight sections (aka octants) */
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

TAILQ_HEAD(Queue, Object);
struct Object {
	/*
	 * An object is any structure that can directly contain a
	 * window of an application.  For example, an open Gimp
	 * application can have two menu windows (on its multi-window
	 * mode), a dialog window, and the main application window.
	 * For each window of that application we have one object:
	 * - The Menu struct for the menu windows.
	 * - The Dialog struct for the dialog window.
	 * - The Tab struct for the main window.
	 *
	 * The application's main window is contained inside a tab,
	 * which is contained inside a row, which is contained inside a
	 * column, which is contained inside a container.  but only the
	 * tab is an object, in the sense that it directly contains a
	 * window of the application.  The row, the column, and the
	 * container are not objects in this sense, because they do not
	 * directly contain an application window (they contain it
	 * indirectly).
	 *
	 * Each object structure begins with an Object struct, so they
	 * can be polymorphically manipulated as its actual type or as
	 * an Object.  For example, we can cast a Dialog into an Object
	 * and vice-versa (if we know that the object is a type).
	 *
	 * The structs that can directly contain an application window
	 * are the following:
	 * - struct Tab;
	 * - struct Dialog;
	 * - struct Menu;
	 * - struct Bar;
	 * - struct Dockapp;
	 * - struct Splash;
	 * - struct Notification;
	 */
	TAILQ_ENTRY(Object) entry;
	Window win;
	int type;
};

TAILQ_HEAD(RowQueue, Row);
struct Row {
	TAILQ_ENTRY(Row) entry;
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
	Window div;                             /* horizontal division between rows */
	Window frame;                           /* where tab frames are */
	Window bar;                             /* title bar frame */
	Window bl;                              /* left button */
	Window br;                              /* right button */

	/*
	 * We only keep the vertical geometry of a row (ie', its y
	 * position and its height), because, since a row horizontally
	 * spans its parent column width, its horizontal geometry is
	 * equal to the geometry of its parent column.
	 */
	double fact;                            /* factor of height relative to container */
	int y, h;                               /* row geometry */

	/*
	 * Three of the windows of the row must be drawn.  First we draw
	 * into their pixmap, and then copy the contents of the pixmap
	 * into the windows thenselves whenever they are damaged.  It is
	 * necessary to redraw on the pixmap only when the row resizes;
	 * so we save the previous width of the row to compare with the
	 * row's current width.
	 */
	Pixmap pixbar;                          /* pixmap for the title bar */
	Pixmap pixbl;                           /* pixmap for left button */
	Pixmap pixbr;                           /* pixmap for right button */
	int pw;

	/*
	 * Whether the frame is unmapped
	 */
	int isunmapped;
};

TAILQ_HEAD(ColumnQueue, Column);
struct Column {
	TAILQ_ENTRY(Column) entry;
	struct RowQueue rowq;                   /* list of rows */

	/*
	 * Each container is split horizontally into columns; and each
	 * column is split vertically into rows.  We maintain in a
	 * column its list of rows, and a pointer to its parent
	 * container.
	 */
	struct Container *c;                    /* pointer to parent container */
	struct Row *selrow;                     /* pointer to selected row */

	/*
	 * At the right of each column, except the rightmost one, there
	 * is a divisor handle which can be dragged to resize the
	 * columns.
	 */
	Window div;                             /* vertical division between columns */

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

TAILQ_HEAD(ContainerQueue, Container);
struct Container {
	/*
	 * The container is the main entity the user interact with, and
	 * the windows of most applications are mapped into a container.
	 *
	 * A container is an element of two queues:
	 * - The focus queue is a list of containers in the focus order.
	 *   There is only one focus queue.
	 * - A raise queue is a list of containers in the Z-axis order.
	 *   There are one raise queue for each layer of containers
	 *   (fullscreen, above, middle and below).
	 */
	TAILQ_ENTRY(Container) entry;           /* entry for the focus queue */
	TAILQ_ENTRY(Container) raiseentry;      /* entry for a raise queue */

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
	struct ColumnQueue colq;                /* list of columns in container */
	struct Column *selcol;                  /* pointer to selected container */
	int ncols;                              /* number of columns */

	/*
	 * A container appears on a certain desktop of a certain monitor.
	 */
	struct Monitor *mon;                    /* monitor container is on */
	int desk;                               /* desktop container is on */

	/*
	 * A container is composed of a frame window, mapped below all
	 * the columns/rows/tabs/etc.  Inside the frame window, there
	 * are mapped the cursor windows, one for each border and corner
	 * of the container's frame.  Each cursor window is associated
	 * to a pointer cursor (that's why hovering the pointer over the
	 * bottom right corner of the frame turns the cursor into an
	 * arrow.
	 */
	Window frame;                           /* window to reparent the contents of the container */
	Window curswin[BORDER_LAST];            /* dummy window used for change cursor while hovering borders */

	/*
	 * The frame must be drawn, with all its borders and corner
	 * handles.  First we draw into a pixmap, and then copy the
	 * contents of the pixmap into the frame window itself whenever
	 * the frame window is damaged.  It is necessary to redraw on
	 * the pixmap only when the container resizes; so we save the
	 * width and height of the pixmap to compare with the size of
	 * the container.
	 */
	Pixmap pix;                             /* pixmap to draw the frame */
	int pw, ph;                             /* pixmap width and height */

	/*
	 * A container has three geometries (position and size): one for
	 * when it is maximized, one for when it is fullscreen, and one
	 * for when it is floating.  The maximized and fullscreen
	 * geometry of a container is obvious (they can be inferred from
	 * the monitor size).  We then save the non-maximized geometry.
	 * We also save the current geometry (which can be one of those
	 * three).
	 */
	int x, y, w, h, b;                      /* current geometry */
	int nx, ny, nw, nh;                     /* non-maximized geometry */

	/*
	 * A container can have several states.  Except for the `layer`
	 * state (which has tree values), all states are boolean (can or
	 * cannot be valid at a given time) and begin with "is-".  The
	 * possible values for boolean states are zero and nonzero.  The
	 * possible values for the `layer` state is negative (below),
	 * zero (middle) or positive (above).
	 */
	int ismaximized, issticky;              /* window states */
	int isminimized, isshaded;              /* window states */
	int isobscured;                         /* whether container is obscured */
	int isfullscreen;                       /* whether container is fullscreen */
	int ishidden;                           /* whether container is hidden */
	int abovebelow;                         /* stacking order */
};

TAILQ_HEAD(MonitorQueue, Monitor);
struct Monitor {
	/*
	 * Each monitor has a focused desktop (a value between 0 and
	 * config.ndesktops - 1).  A monitor also has two geometries:
	 * its full actual geometry (a rectangle spanning the entire
	 * monitor), and the window area (a rectangle spanning only
	 * the area without any dock, bar, panel, etc (that is, the
	 * area where containers can be maximized into).
	 */
	TAILQ_ENTRY(Monitor) entry;
	int seldesk;                            /* focused desktop on that monitor */
	int mx, my, mw, mh;                     /* monitor size */
	int wx, wy, ww, wh;                     /* window area */
};

struct Tab {
	struct Object obj;

	/*
	 * Additionally to the application window (in .obj), a tab
	 * contains a list of swallowed dialogs (unless -d is given) and
	 * a list of detached menus.  A tab also contains a pointer to
	 * its parent row.
	 */
	struct Queue dialq;                     /* queue of dialogs */
	struct Row *row;                        /* pointer to parent row */

	/*
	 * The application whose windows the tab maintains can be
	 * grouped under a leader window (which is not necessarily
	 * mapped on the screen).
	 */
	Window leader;                          /* the group leader of the window */

	/*
	 * Visually, a tab is composed of a title bar (aka tab); and a
	 * frame window which the application window and swallowed
	 * dialog windows are child of.
	 */
	Window title;                           /* title bar (tab) */
	Window frame;                           /* window to reparent the client window */

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

	/*
	 * Dirty hack to ignore unmap notifications.
	 */
	int ignoreunmap;                        /* number of unmapnotifys to ignore */

	/*
	 * Name of the tab's application window, its size and urgency.
	 */
	int winw, winh;                         /* window geometry */
	int isurgent;                           /* whether tab is urgent */
	char *name;                             /* client name */
};

struct Dialog {
	struct Object obj;
	struct Tab *tab;                        /* pointer to parent tab */

	/*
	 * Frames, pixmaps, saved pixmap geometry, etc
	 */
	Window frame;                           /* window to reparent the client window */
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

struct Menu {
	struct Object obj;
	struct Monitor *mon;
	Window leader;

	/*
	 * Frames, pixmaps, saved pixmap geometry, etc
	 */
	Window titlebar;                        /* close button */
	Window button;                          /* close button */
	Window frame;                           /* frame window */
	Pixmap pix;                             /* pixmap to draw the frame */
	Pixmap pixbutton;                       /* pixmap to draw the button */
	Pixmap pixtitlebar;                     /* pixmap to draw the titlebar */
	int pw, ph;                             /* pixmap size */
	int tw, th;                             /* titlebar pixmap size */

	int x, y, w, h;                         /* geometry of the menu window + the frame */
	int ignoreunmap;                        /* number of unmapnotifys to ignore */
	char *name;                             /* client name */
};

struct Bar {
	struct Object obj;
	int strut[STRUT_LAST];                  /* strut values */
	int ispartial;                          /* whether strut has 12 elements rather than 4 */
};

struct Dockapp {
	struct Object obj;
	int x, y, w, h;                 /* dockapp position and size */
	int ignoreunmap;                /* number of unmap requests to ignore */
	int dockpos;                    /* position of the dockapp in the dock */
	int state;                      /* dockapp state */
	int slotsize;                   /* size of the slot the dockapp is in */
};

struct Splash {
	struct Object obj;
	struct Monitor *mon;
	Window frame;
	int desk;
	int x, y, w, h;                         /* splash screen geometry */
};

struct Notification {
	struct Object obj;
	Window frame;                           /* window to reparent the actual client window */
	Pixmap pix;                             /* pixmap to draw the frame */
	int w, h;                               /* geometry of the entire thing (content + decoration) */
	int pw, ph;                             /* pixmap width and height */
};

struct WM {
	int running;

	/*
	 * The window manager maintains a list of monitors and several
	 * window-holding entities such as containers and bars.
	 */
	struct MonitorQueue monq;               /* queue of monitors */
	struct Queue barq;                      /* queue of bars */
	struct Queue splashq;                   /* queue of splash screen windows */
	struct Queue notifq;                    /* queue of notifications */
	struct Queue menuq;                     /* queue of menus */
	struct ContainerQueue focusq;           /* queue of containers ordered by focus history */
	int nclients;                           /* total number of container windows */

	/*
	 * Resources
	 */
	struct {
		XrmClass class;
		XrmName name;
	} application, resources[NRESOURCES];
	XrmQuark anyresource;

	/*
	 * Xrandr information.
	 */
	int xrandr;                             /* whether Xrandr is being used */
	int xrandrev;                           /* event base for Xrandr */

	/*
	 * Containers are listed by the focusq queue; they are also
	 * listed under the stackq list, ordered by its position on
	 * the Z-axis.
	 *
	 * Since there are 4 layers on the Z-axis and we often need
	 * to move a container to the top of its layer, we have four
	 * "dummy" containers used as placeholder as the top of each
	 * layer on the stackq list.
	 *
	 * There is also a dummy window to place the dock.
	 */
	struct ContainerQueue stackq;
	struct Container layers[LAYER_LAST];
	Window docklayer;                       /* dummy window used to set dock layer */

	/*
	 * We maintain a pointer to the focused container and the
	 * previously focused one.  And also a pointer to the focused
	 * monitor which will receive new containers.
	 */
	struct Container *focused;              /* pointer to focused container */
	struct Container *prevfocused;          /* pointer to previously focused container */
	struct Monitor *selmon;                 /* pointer to selected monitor */

	/*
	 * Dummy windows
	 */
	Window checkwin;                        /* carries _NET_SUPPORTING_WM_CHECK */
	Window focuswin;                        /* gets focus when no container is visible */
	Window dragwin;                         /* follows mouse while dragging */
	Window restackwin;                      /* reordered in Z axis to save a position */

	Cursor cursors[CURSOR_LAST];            /* cursors for the mouse pointer */
	int showingdesk;                        /* whether the desktop is being shown */
	int minsize;                            /* minimum size of a container */

	/*
	 * Some operations need to reset the list of clients, to do it
	 * only once when more than one of such operations, we use this
	 * value we check at the end of each main loop iteration and
	 * reset at the beginning of each main loop iteration
	 */
	int setclientlist;

	Window presswin;
};

struct Dock {
	/* the dock */
	struct Queue dappq;
	Window win;                     /* dock window */
	Pixmap pix;                     /* dock pixmap */
	int x, y, w, h;                 /* dock geometry */
	int pw, ph;                     /* dock pixmap size */
	int mapped;                     /* whether dock is mapped */
};

struct Config {
	/* default configuration, set at `config.c` */

	KeySym altkeysym;                       /* key to trigger alt-tab */
	KeySym tabkeysym;                       /* key to cycle alt-tab */

	int disablehidden;                      /* whether -h is passed */
	int disablealttab;                      /* whether -t is passed */
	int floatdialog;                        /* whether -d is passed */
	int honorconfig;                        /* whether -c is passed */
	int sloppyfocus;                        /* whether -s is passed */
	int sloppytiles;                        /* whether -S is passed */
	int ndesktops;                          /* number of desktops */
	int notifgap;                           /* gap between notifications */
	int dockwidth, dockspace;               /* dock geometry */
	int snap;                               /* snap proximity */
	int borderwidth;                        /* width of the border frame */
	int titlewidth;                         /* height of the title bar */
	int shadowthickness;                    /* thickness of the 3D shadows */
	int movetime;                           /* time (ms) to redraw containers during moving */
	int resizetime;                         /* time (ms) to redraw containers during resizing */

	char *menucmd;                          /* command to spawn when clicking the menu button */

	/* gravities (N for north, NE for northeast, etc) */
	const char *notifgravity;
	const char *dockgravity;

	/* font and color names */
	const char *font;
	const char *colors[STYLE_LAST][COLOR_LAST];

	/* hardcoded rules */
	struct Rule {
		/* matching class, instance and role */
		const char *class;
		const char *instance;
		const char *role;

		/* type, state, etc to apply on matching windows */
		int type;
		int state;
		int desktop;
	} *rules;

	/* the values below are computed from the values above */
	KeyCode altkeycode;                     /* keycode of the altkeysym */
	KeyCode tabkeycode;                     /* keycode of the tabkeysym */
	unsigned int modifier;                  /* modifier of the altkeycode */
	int corner;                             /* = .borderwidth + .titlewidth */
	int divwidth;                           /* = .borderwidth */
};

typedef void Managefunc(struct Tab *, struct Monitor *, int, Window, Window, XRectangle, int, int);
typedef int Unmanagefunc(struct Object *obj, int ignoreunmap);

/* container routines */
struct Container *getnextfocused(struct Monitor *mon, int desk);
struct Container *containerraisetemp(struct Container *prevc, int backward);
void containernewwithtab(struct Tab *tab, struct Monitor *mon, int desk, XRectangle rect, int state);
void containerbacktoplace(struct Container *c, int restack);
void containerdel(struct Container *c);
void containermoveresize(struct Container *c, int checkstack);
void containerdecorate(struct Container *c, struct Column *cdiv, struct Row *rdiv, int recursive, enum Octant o);
void containerredecorate(struct Container *c, struct Column *cdiv, struct Row *rdiv, enum Octant o);
void containercalccols(struct Container *c);
void containersetstate(struct Tab *, Atom *, unsigned long);
void containerincrmove(struct Container *c, int x, int y);
void containerraise(struct Container *c, int isfullscreen, int layer);
void containerconfigure(struct Container *c, unsigned int valuemask, XWindowChanges *wc);
void containersendtodeskandfocus(struct Container *c, struct Monitor *mon, unsigned long desk);
void containerplace(struct Container *c, struct Monitor *mon, int desk, int userplaced);
void containerdelrow(struct Row *row);
void containerhide(struct Container *c, int hide);
void tabdetach(struct Tab *tab, int x, int y);
void tabfocus(struct Tab *tab, int gotodesk);
void tabdecorate(struct Tab *t, int pressed);
void tabupdateurgency(struct Tab *t, int isurgent);
void rowstretch(struct Column *col, struct Row *row);
void dialogconfigure(struct Dialog *d, unsigned int valuemask, XWindowChanges *wc);
void dialogmoveresize(struct Dialog *dial);
int tabattach(struct Container *c, struct Tab *t, int x, int y);
int containerisshaded(struct Container *c);
int containerisvisible(struct Container *c, struct Monitor *mon, int desk);
int columncontentheight(struct Column *col);
int containercontentwidth(struct Container *c);

/* menu */
void menufocus(struct Menu *menu);
void menuhide(struct Menu *menu, int hide);
void menuincrmove(struct Menu *menu, int x, int y);
void menuconfigure(struct Menu *menu, unsigned int valuemask, XWindowChanges *wc);
void menumoveresize(struct Menu *menu);
void menudecorate(struct Menu *menu, int titlepressed);
void menufocusraise(struct Menu *menu);
void menuraise(struct Menu *menu);
void menuplace(struct Monitor *mon, struct Menu *menu);
void menuupdate(void);
int istabformenu(struct Tab *tab, struct Menu *menu);

/* other object routines */
void dockappconfigure(struct Dockapp *dapp, unsigned int valuemask, XWindowChanges *wc);
void barstrut(struct Bar *bar);
void notifplace(void);
void notifdecorate(struct Notification *n);
void splashplace(struct Monitor *mon, struct Splash *splash);
void splashhide(struct Splash *splash, int hide);
void splashrise(struct Splash *splash);
void dockupdate(void);
void dockdecorate(void);
void dockreset(void);

/* monitor routines */
struct Monitor *getmon(int x, int y);
void mondel(struct Monitor *mon);
void monupdate(void);
void monupdatearea(void);
void fitmonitor(struct Monitor *mon, int *x, int *y, int *w, int *h, float factor);
void moninit(void);
void monevent(XEvent *e);

/* wm hints and messages routines */
void icccmdeletestate(Window win);
void icccmwmstate(Window win, int state);
void ewmhinit(const char *wmname);
void ewmhsetdesktop(Window win, long d);
void ewmhsetframeextents(Window win, int b, int t);
void ewmhsetclients(void);
void ewmhsetstate(struct Container *c);
void ewmhsetwmdesktop(struct Container *c);
void ewmhsetactivewindow(Window w);
void ewmhsetcurrentdesktop(unsigned long n);
void ewmhsetshowingdesktop(int n);
void shodgrouptab(struct Container *c);
void shodgroupcontainer(struct Container *c);
void winupdatetitle(Window win, char **name);
void winnotify(Window win, int x, int y, int w, int h);
void winclose(Window win);

/* decoration routines */
void pixmapnew(Pixmap *pix, Window win, int w, int h);
void drawcommit(Pixmap pix, Window win);
void drawborders(Pixmap pix, int w, int h, int style);
void drawbackground(Pixmap pix, int x, int y, int w, int h, int style);
void drawframe(Pixmap pix, int isshaded, int w, int h, enum Octant o, int style);
void drawshadow(Pixmap pix, int x, int y, int w, int h, int style, int pressed);
void drawtitle(Drawable pix, const char *text, int w, int drawlines, int style, int pressed, int ismenu);
void drawprompt(Pixmap pix, int w, int h);
void drawdock(Pixmap pix, int w, int h);
void buttonleftdecorate(Window button, Pixmap pix, int style, int pressed);
void buttonrightdecorate(Window button, Pixmap pix, int style, int pressed);
void cleantheme(void);
void setresources(char *xrm);
int settheme(void);

/* window management routines */
Managefunc managedockapp;
Managefunc managedialog;
Managefunc managesplash;
Managefunc manageprompt;
Managefunc managenotif;
Managefunc managemenu;
Managefunc managecontainer;
Managefunc managebar;
Unmanagefunc unmanagedockapp;
Unmanagefunc unmanagedialog;
Unmanagefunc unmanagesplash;
Unmanagefunc unmanageprompt;
Unmanagefunc unmanagenotif;
Unmanagefunc unmanagemenu;
Unmanagefunc unmanagecontainer;
Unmanagefunc unmanagebar;
void setmod(void);
void scan(void);
void deskupdate(struct Monitor *mon, int desk);
int getwintype(Window *win_ret, Window *leader, struct Tab **tab, int *state, XRectangle *rect, int *desk);

/* function tables */
extern void (*managefuncs[])(struct Tab *, struct Monitor *, int, Window, Window, XRectangle, int, int);
extern int (*unmanagefuncs[])(struct Object *, int);

/* extern variables */
extern XSetWindowAttributes clientswa;
extern unsigned long clientmask;
extern void (*xevents[LASTEvent])(XEvent *);
extern struct Config config;
extern struct WM wm;
extern struct Dock dock;
