#include <sys/queue.h>

#include "xutil.h"

#define TAB_FOREACH_BEGIN(c, tab) {                             \
	struct Object *col, *row;                                     \
	TAILQ_FOREACH_REVERSE(col, &(c)->colq, Queue, entry) {           \
		TAILQ_FOREACH_REVERSE(row, &((struct Column *)col)->rowq, Queue, entry) {      \
			TAILQ_FOREACH_REVERSE(tab, &((struct Row *)row)->tabq, Queue, entry)
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

enum border {
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
	FOCUSED    = 0,
	UNFOCUSED  = 1,
	URGENT     = 2,
	PRESSED    = 3,
	STYLE_LAST = 4,

#warning TODO: implement pressed state
	STYLE_OTHER = PRESSED,
};

enum {
	/* window layer array indices */
	LAYER_DESK,
	LAYER_BELOW,
	LAYER_NORMAL,
	LAYER_ABOVE,
	LAYER_MENU,
	LAYER_DOCK,
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

enum State {
	/* container states bits*/
	ABOVE           = 0x001,
	BELOW           = 0x002,
	FULLSCREEN      = 0x004,
	MAXIMIZED       = 0x008,
	MINIMIZED       = 0x010,
	SHADED          = 0x020,
	STICKY          = 0x040,
	USERPLACED      = 0x080,
	ATTENTION       = 0x100,
	STRETCHED       = 0x200,

	/* dockapp states bits */
	EXTEND          = 0x001,
	SHRUNK          = 0x002,
	RESIZED         = 0x004,
};

enum {
	/* container state action */
	REMOVE = 0,
	ADD    = 1,
	TOGGLE = 2
};

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

TAILQ_HEAD(Queue, Object);
struct Object {
	TAILQ_ENTRY(Object) entry;
	Window win;
	struct Class *class;
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
	 * Whether the frame is unmapped
	 */
	int isunmapped;
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

TAILQ_HEAD(ContainerQueue, Container);
struct Container {
	struct Object obj;

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
	struct Queue colq;                      /* list of columns in container */
	struct Column *selcol;                  /* pointer to selected container */
	int ncols;                              /* number of columns */

	/*
	 * A container appears on a certain desktop of a certain monitor.
	 */
	struct Monitor *mon;                    /* monitor container is on */
	int desk;                               /* desktop container is on */

	Window borders[BORDER_LAST];

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
	 * Container state bitmask.
	 */
	enum State state;
	Bool ishidden;                          /* whether container is hidden */
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

struct Class {
	/* class methods */
	void (*init)(void);
	void (*clean)(void);
	void (*manage)(struct Tab *, struct Monitor *, int, Window, Window, XRectangle, enum State);
	void (*restack)(void);
	void (*monitor_delete)(struct Monitor *);
	void (*monitor_reset)(void);
	void (*redecorate_all)(void);
	void (*show_desktop)(void);
	void (*hide_desktop)(void);
	void (*change_desktop)(struct Monitor *, int, int);

	/* instance methods */
	void (*setstate)(struct Object *, enum State, int);
	void (*unmanage)(struct Object *);
	void (*btnpress)(struct Object *, XButtonPressedEvent *);
	void (*handle_property)(struct Object *, Atom);
	void (*handle_message)(struct Object *, Atom, long[5]);
	void (*handle_configure)(struct Object *, unsigned int, XWindowChanges *);
	void (*handle_enter)(struct Object *);
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
	Pixmap pixtitlebar;                     /* pixmap to draw the titlebar */
	int pw, ph;                             /* pixmap size */
	int tw;                                 /* titlebar pixmap size */

	int x, y, w, h;                         /* geometry of the menu window + the frame */
	int ignoreunmap;                        /* number of unmapnotifys to ignore */
	char *name;                             /* client name */
};

struct Bar {
	struct Object obj;
	struct Monitor *mon;
	int strut[STRUT_LAST];                  /* strut values */
	Bool ispartial;                         /* whether strut has 12 elements rather than 4 */
	enum State state;
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
	int desk;
	int x, y, w, h;
};

struct Notification {
	struct Object obj;
	Window frame;                           /* window to reparent the actual client window */
	Pixmap pix;                             /* pixmap to draw the frame */
	int w, h;                               /* geometry of the entire thing (content + decoration) */
	int pw, ph;                             /* pixmap width and height */
};

struct WM {
	Bool running;

	struct MonitorQueue monq;               /* queue of monitors */
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
#warning TODO: remove the .layers member; use separate lists of containers instead
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
	 * Whenever a function adds or removes a client, this value is
	 * set.  At the end of each main loop iteration, it is checked
	 * and the list of clients is changed accordingly.
	 */
	Bool setclientlist;

	Window presswin;

	struct {
		Pixmap btn_left;
		Pixmap btn_right;
		Pixmap bar_vert;
		Pixmap bar_horz;
		Pixmap corner_nw;
		Pixmap corner_ne;
		Pixmap corner_sw;
		Pixmap corner_se;
	} decorations[STYLE_LAST];
};

struct Dock {
	struct Object obj;

	struct Queue dappq;
	Pixmap pix;                     /* dock pixmap */
	int x, y, w, h;                 /* dock geometry */
	int pw, ph;                     /* dock pixmap size */
	enum State state;
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
		struct Class *type;
		int state;
		int desktop;
	} *rules;

	/* the values below are computed from the values above */
	unsigned int modifier;                  /* modifier of the altkeycode */
	int corner;                             /* = .borderwidth + .titlewidth */
	int divwidth;                           /* = .borderwidth */
};

struct Container *getnextfocused(struct Monitor *mon, int desk);
void alttab(KeyCode altkey, KeyCode tabkey, Bool shift);
void tabfocus(struct Tab *tab, int gotodesk);
struct Tab *gettabfrompid(unsigned long pid);
struct Tab *getleaderof(Window leader);
int containerisvisible(struct Container *c, struct Monitor *mon, int desk);

/* menu */
void menufocus(struct Menu *menu);
void menuconfigure(struct Menu *menu, unsigned int valuemask, XWindowChanges *wc);
void menumoveresize(struct Menu *menu);
void menudecorate(struct Menu *menu);
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
void dockdecorate(void);
void dockreset(void);
void dockstack(void);

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
void shoddocks(void);
void shodgrouptab(struct Container *c);
void shodgroupcontainer(struct Container *c);
void winupdatetitle(Window win, char **name);
void winnotify(Window win, int x, int y, int w, int h);
void winclose(Window win);
void unmapwin(Window win);
void mapwin(Window win);

/* decoration routines */
Window createframe(XRectangle geom);
Window createdecoration(Window frame, XRectangle geom, Cursor curs, int gravity);
void updatepixmap(Pixmap *pix, int *pixw, int *pixh, int w, int h);
void drawcommit(Pixmap pix, Window win);
void backgroundcommit(Window, int style);
void drawborders(Pixmap pix, int w, int h, int style);
void drawbackground(Pixmap pix, int x, int y, int w, int h, int style);
void drawshadow(Pixmap pix, int x, int y, int w, int h, int style, int pressed, int thickness);
void drawtitle(Drawable pix, const char *text, int w, int drawlines, int style, int pressed, int ismenu);
void drawprompt(Pixmap pix, int w, int h);
void redecorate(Window win, int border, int style, Bool pressed);
void cleantheme(void);
void setresources(char *xrm);
void initdepth(void);
void inittheme(void);

/* window management routines */
void setmod(void);
void scan(void);
void deskupdate(struct Monitor *mon, int desk);
int getwintype(Window win, Window *leader, struct Tab **tab, int *state, XRectangle *rect, int *desk);
struct Class *getwinclass(Window win, Window *leader, struct Tab **tab,
                          enum State *state, XRectangle *rect, int *desk);


void window_del(Window);
void context_add(XID, struct Object *);
void context_del(XID);
struct Object *context_get(XID);
Bool isvalidstate(unsigned int state);

/* extern variables */
extern XContext context;
extern void (*xevents[LASTEvent])(XEvent *);
extern struct Config config;
extern struct WM wm;
extern struct Dock dock;

/* object classes */
extern struct Class bar_class;
extern struct Class dialog_class;
extern struct Class dock_class;
extern struct Class dockapp_class;
extern struct Class menu_class;
extern struct Class notif_class;
extern struct Class prompt_class;
extern struct Class splash_class;
extern struct Class tab_class;
extern struct Class container_class;
