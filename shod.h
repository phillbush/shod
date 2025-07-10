#include <sys/queue.h>

#include "xutil.h"

#define MOUSE_EVENTS (ButtonReleaseMask|ButtonPressMask|PointerMotionMask)

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
	LAYER_DESK,
	LAYER_BELOW,
	LAYER_NORMAL,
	LAYER_ABOVE,
	LAYER_MENU,
	LAYER_DOCK,
	LAYER_LAST
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
	TAILQ_ENTRY(Object) z_entry;
	Window win;
	struct Class *class;
	void *self;
};

struct Monitor {
	/*
	 * Interned atom identifying the monitor.
	 */
	Atom name;

	/*
	 * Focused desktop (a value between 0 and config.ndesktops-1).
	 * Each monitor has a separate focused desktop.
	 */
	int seldesk;                            /* focused desktop on that monitor */

	/*
	 * Monitor area: a rectangle spanning the entire monitor.
	 */
	int mx, my, mw, mh;                     /* monitor size */

	/*
	 * Window area: a rectangle spanning only the region within the
	 * monitor without any dock/bar/panel (that is, the region where
	 * containers can be maximized into).
	 */
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
	unsigned int pid;

	/*
	 * Name of the tab's application window, its size and urgency.
	 */
	int winw, winh;                         /* window geometry */
	Bool isurgent;                          /* whether tab is urgent */
	char *name;                             /* client name */
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
	void (*reload_theme)(void);
	void (*show_desktop)(void);
	void (*hide_desktop)(void);
	void (*change_desktop)(struct Monitor *, int, int);
	void (*list_clients)(void);

	/* instance methods */
	void (*focus)(struct Object *);
	void (*setstate)(struct Object *, enum State, int);
	void (*unmanage)(struct Object *);
	void (*btnpress)(struct Object *, XButtonPressedEvent *);
	void (*redecorate)(struct Object *);
	void (*handle_property)(struct Object *, Atom);
	void (*handle_message)(struct Object *, Atom, long[5]);
	void (*handle_configure)(struct Object *, unsigned int, XWindowChanges *);
	void (*handle_enter)(struct Object *);
};

struct WM {
	int nmonitors;
	struct Monitor **monitors;
	struct Monitor *selmon;

	Window layertop[LAYER_LAST];
	struct Object *focused;                 /* pointer to focused container */

	Cursor cursors[CURSOR_LAST];            /* cursors for the mouse pointer */
	int showingdesk;                        /* whether the desktop is being shown */
	int minsize;                            /* minimum size of a container */

	/*
	 * Dummy windows
	 */
	Window checkwin;                        /* carries _NET_SUPPORTING_WM_CHECK */
	Window focuswin;                        /* gets focus when no container is visible */
	Window dragwin;                         /* follows mouse while dragging */
	Window restackwin;                      /* reordered in Z axis to save a position */

	/*
	 * Whenever a function adds or removes a client, this value is
	 * set.  At the end of each main loop iteration, it is checked
	 * and the list of clients is changed accordingly.
	 */
	Bool setclientlist;
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

struct Theme {
	XftFont *font;
	XftColor colors[STYLE_LAST][COLOR_LAST];
};

void focusnext(struct Monitor *mon, int desk);
void alttab(KeyCode altkey, KeyCode tabkey, Bool shift);
struct Tab *gettabfrompid(unsigned long pid);
struct Tab *getleaderof(Window leader);
Bool focused_follows_leader(Window leader);
Bool focused_is_fullscreen(void);

/* wm hints and messages routines */
void icccmdeletestate(Window win);
void icccmwmstate(Window win, int state);
void ewmhsetframeextents(Window win, int b, int t);
void shoddocks(void);
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

void fitmonitor(struct Monitor *mon, int *x, int *y, int *w, int *h, float factor);
struct Monitor *getmon(int x, int y);
void deskupdate(struct Monitor *mon, long desk);

Bool isvalidstate(unsigned int state);

void context_add(XID, struct Object *);
void context_del(XID);
struct Object *context_get(XID);
void window_del(Window);
void window_close(Display *, Window win);

/* extern variables */
extern Display *dpy;
extern Window root;
extern Atom atoms[NATOMS];
extern int screen;
extern struct Config config;
extern struct WM wm;
extern Visual *visual;
extern Colormap colormap;
extern unsigned int depth;
extern XrmDatabase xdb;
extern GC gc;
extern struct Theme theme;

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
