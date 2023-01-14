#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/xpm.h>
#include <X11/Xft/Xft.h>

/* atom names */
enum {
	/* utf8 */
	UTF8_STRING,

	/* ICCCM atoms */
	WM_DELETE_WINDOW,
	WM_WINDOW_ROLE,
	WM_TAKE_FOCUS,
	WM_PROTOCOLS,
	WM_STATE,
	WM_CLIENT_LEADER,

	/* EWMH atoms */
	_NET_ACTIVE_WINDOW,
	_NET_CLIENT_LIST,
	_NET_CLIENT_LIST_STACKING,
	_NET_CLOSE_WINDOW,
	_NET_CURRENT_DESKTOP,
	_NET_DESKTOP_NAMES,
	_NET_FRAME_EXTENTS,
	_NET_MOVERESIZE_WINDOW,
	_NET_NUMBER_OF_DESKTOPS,
	_NET_REQUEST_FRAME_EXTENTS,
	_NET_SHOWING_DESKTOP,
	_NET_SUPPORTED,
	_NET_SUPPORTING_WM_CHECK,
	_NET_WM_DESKTOP,
	_NET_WM_FULL_PLACEMENT,
	_NET_WM_MOVERESIZE,
	_NET_WM_NAME,
	_NET_WM_STATE,
	_NET_WM_STATE_ABOVE,
	_NET_WM_STATE_BELOW,
	_NET_WM_STATE_DEMANDS_ATTENTION,
	_NET_WM_STATE_FOCUSED,
	_NET_WM_STATE_FULLSCREEN,
	_NET_WM_STATE_HIDDEN,
	_NET_WM_STATE_MAXIMIZED_HORZ,
	_NET_WM_STATE_MAXIMIZED_VERT,
	_NET_WM_STATE_SHADED,
	_NET_WM_STATE_STICKY,
	_NET_WM_STRUT,
	_NET_WM_STRUT_PARTIAL,
	_NET_WM_WINDOW_TYPE,
	_NET_WM_WINDOW_TYPE_DESKTOP,
	_NET_WM_WINDOW_TYPE_DIALOG,
	_NET_WM_WINDOW_TYPE_DOCK,
	_NET_WM_WINDOW_TYPE_MENU,
	_NET_WM_WINDOW_TYPE_NOTIFICATION,
	_NET_WM_WINDOW_TYPE_PROMPT,
	_NET_WM_WINDOW_TYPE_SPLASH,
	_NET_WM_WINDOW_TYPE_TOOLBAR,
	_NET_WM_WINDOW_TYPE_UTILITY,

	/* motif atoms */
	_MOTIF_WM_HINTS,

	/* shod atoms */
	_SHOD_GROUP_TAB,
	_SHOD_GROUP_CONTAINER,
	_SHOD_CONTAINER_LIST,

	ATOM_LAST
};

extern Visual *visual;
extern Colormap colormap;
extern unsigned int depth;
extern XrmDatabase xdb;
extern Display *dpy;
extern Window root;
extern Atom atoms[ATOM_LAST];
extern int screen;

int max(int x, int y);
int min(int x, int y);
void *ecalloc(size_t nmemb, size_t size);
void *emalloc(size_t size);
char *estrndup(const char *s, size_t maxlen);

unsigned long getwinsprop(Window win, Atom prop, Window **wins);
unsigned long getcardsprop(Window win, Atom prop, unsigned long **array);
unsigned long getcardprop(Window win, Atom prop);
unsigned long getatomsprop(Window win, Atom prop, Atom **atoms);
Window getwinprop(Window win, Atom prop);
Atom getatomprop(Window win, Atom prop);
void initatoms(void);
void initatom(int atomenum);
void xinit(void);
void xinitvisual(void);
