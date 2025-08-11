#include <sys/queue.h>

#include "util.h"

#define MOUSE_EVENTS (ButtonReleaseMask|ButtonPressMask|PointerMotionMask)

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
	COLOR_BODY,
	COLOR_LIGHT,
	COLOR_DARK,
	COLOR_LAST,

	COLOR_FG = COLOR_LIGHT,
};

enum {
	/* decoration style array indices */
	FOCUSED    = 0,
	UNFOCUSED  = 1,
	URGENT     = 2,
	STYLE_OTHER = 3,
	STYLE_LAST = 4,
};

enum {
	LAYER_DESK,
	LAYER_BELOW,
	LAYER_NORMAL,
	LAYER_ABOVE,
	LAYER_MENU,
	LAYER_DOCK,
	LAYER_FULLSCREEN,
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

	XRectangle geometry;
	XRectangle window_area;
};

struct Class {
	/* class methods */
	void (*init)(void);
	void (*clean)(void);
	void (*manage)(struct Object *, struct Monitor *, int, Window, Window, XRectangle, enum State);
	void (*restack_all)(void);
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
	void (*restack)(struct Object *);
	void (*setstate)(struct Object *, enum State, int);
	void (*unmanage)(struct Object *);
	void (*btnpress)(struct Object *, XButtonPressedEvent *);
	void (*redecorate)(struct Object *);
	void (*handle_property)(struct Object *, Atom);
	void (*handle_message)(struct Object *, Atom, long[5]);
	void (*handle_configure)(struct Object *, unsigned int, XWindowChanges *);
	void (*handle_enter)(struct Object *);
};

struct Theme {
	XftFont *font;
	XftColor colors[STYLE_LAST][COLOR_LAST];
};

struct WM {
	Display *display;
	Window rootwin;
	Atom atoms[NATOMS];
	int screen;
	Visual *visual;
	Colormap colormap;
	unsigned int depth;
	struct Theme theme;

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

	Pixmap close_btn[STYLE_LAST][2];
	GC gc;
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
	int button_size;
	int movetime;                           /* time (ms) to redraw containers during moving */
	int resizetime;                         /* time (ms) to redraw containers during resizing */

	char *menucmd;                          /* command to spawn when clicking the menu button */

	/* gravities (N for north, NE for northeast, etc) */
	const char *notifgravity;
	const char *dockgravity;

	/* font and color names */
	const char *font;
	const char *colors[STYLE_LAST][COLOR_LAST];

	/* the values below are computed from the values above */
	unsigned int modifier;                  /* modifier of the altkeycode */
	int corner;                             /* = .borderwidth + .titlewidth */
	int divwidth;                           /* = .borderwidth */
};

void focusnext(struct Monitor *mon, int desk);
void alttab(KeyCode altkey, KeyCode tabkey, Bool shift);
Bool focused_follows_leader(Window leader);
void shoddocks(void);
void winupdatetitle(Window win, char **name);
void window_configure_notify(Display *display, Window window, int x, int y, int w, int h);

/* decoration routines */
Window createwindow(Window parent, XRectangle geom, long mask, XSetWindowAttributes *attrs);
Window createframe(XRectangle geom);
Window createdecoration(Window frame, XRectangle geom, Cursor curs, int gravity);
void updatepixmap(Pixmap *pix, int *pixw, int *pixh, int w, int h);
void drawcommit(Pixmap pix, Window win);
void backgroundcommit(Window, int style);
void drawborders(Pixmap pix, int w, int h, int style);
void drawbackground(Pixmap pix, int x, int y, int w, int h, int style);
void drawshadow(Pixmap pix, int x, int y, int w, int h, int style);
void drawtitle(Drawable pix, const char *text, int w, int drawlines, int style, int pressed, int ismenu);
void drawprompt(Pixmap pix, int w, int h);

void fitmonitor(struct Monitor *mon, XRectangle *geometry, float factor);
struct Monitor *getmon(int x, int y);
void deskupdate(struct Monitor *mon, long desk);
void deskshow(Bool show);

Bool isvalidstate(unsigned int state);

void context_add(XID, struct Object *);
void context_del(XID);
struct Object *context_get(XID);
void window_close(Display *, Window win);

extern struct WM wm;
extern struct Config config;

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

/* overridden XLib routines */

#define XNextEvent(dpy, evp) do { \
	XNextEvent((dpy), (evp)); \
	if ((evp)->type != MotionNotify || compress_motion((dpy), (evp))) \
		break; \
	} while(1)

#define XMaskEvent(dpy, mask, evp) do { \
	XMaskEvent((dpy), (mask), (evp)); \
	if ((evp)->type != MotionNotify || compress_motion((dpy), (evp))) \
		break; \
	} while(1)

#define XMapWindow(dpy, win) do { \
	XMapWindow((dpy), (win)); \
	XChangeProperty( \
		dpy, win, wm.atoms[WM_STATE], wm.atoms[WM_STATE], \
		32, PropModeReplace, (void *)&(long[]){ \
			[0] = NormalState, \
			[1] = None, \
		}, 2 \
	); \
} while(0)

#define XUnmapWindow(dpy, win) do { \
	XWindowAttributes _attrs = {0}; \
	if (!XGetWindowAttributes((dpy), (win), &_attrs)) \
		_attrs.your_event_mask = 0; \
	XSelectInput((dpy), (win), _attrs.your_event_mask & ~StructureNotifyMask); \
	XUnmapWindow((dpy), (win)); \
	XSelectInput((dpy), (win), _attrs.your_event_mask); \
	XChangeProperty( \
		dpy, win, wm.atoms[WM_STATE], wm.atoms[WM_STATE], \
		32, PropModeReplace, (void *)&(long[]){ \
			[0] = IconicState, \
			[1] = None, \
		}, 2 \
	); \
} while(0)

#define XDestroyWindow(dpy, win) do { \
	context_del((win)); \
	XDestroyWindow((dpy), (win)); \
} while(0)
