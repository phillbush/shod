#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/xpm.h>
#include <X11/Xft/Xft.h>

#define LEN(x)                  (sizeof(x) / sizeof((x)[0]))
#define FLAG(f, b)              (((f) & (b)) == (b))

/* atom names */
#define ATOMS \
	X(UTF8_STRING) \
	X(WM_DELETE_WINDOW) \
	X(WM_WINDOW_ROLE) \
	X(WM_TAKE_FOCUS) \
	X(WM_PROTOCOLS) \
	X(WM_STATE) \
	X(WM_CLIENT_LEADER) \
	X(_NET_ACTIVE_WINDOW) \
	X(_NET_CLIENT_LIST) \
	X(_NET_CLIENT_LIST_STACKING) \
	X(_NET_CLOSE_WINDOW) \
	X(_NET_CURRENT_DESKTOP) \
	X(_NET_DESKTOP_NAMES) \
	X(_NET_FRAME_EXTENTS) \
	X(_NET_MOVERESIZE_WINDOW) \
	X(_NET_NUMBER_OF_DESKTOPS) \
	X(_NET_REQUEST_FRAME_EXTENTS) \
	X(_NET_SHOWING_DESKTOP) \
	X(_NET_SUPPORTED) \
	X(_NET_SUPPORTING_WM_CHECK) \
	X(_NET_WM_PID) \
	X(_NET_WM_DESKTOP) \
	X(_NET_WM_FULL_PLACEMENT) \
	X(_NET_WM_MOVERESIZE) \
	X(_NET_WM_NAME) \
	X(_NET_WM_ICON_NAME) \
	X(_NET_WM_STATE) \
	X(_NET_WM_STATE_ABOVE) \
	X(_NET_WM_STATE_BELOW) \
	X(_NET_WM_STATE_DEMANDS_ATTENTION) \
	X(_NET_WM_STATE_FOCUSED) \
	X(_NET_WM_STATE_FULLSCREEN) \
	X(_NET_WM_STATE_HIDDEN) \
	X(_NET_WM_STATE_MAXIMIZED_HORZ) \
	X(_NET_WM_STATE_MAXIMIZED_VERT) \
	X(_NET_WM_STATE_SHADED) \
	X(_NET_WM_STATE_STICKY) \
	X(_NET_WM_STRUT) \
	X(_NET_WM_STRUT_PARTIAL) \
	X(_NET_WM_WINDOW_TYPE) \
	X(_NET_WM_WINDOW_TYPE_DESKTOP) \
	X(_NET_WM_WINDOW_TYPE_DIALOG) \
	X(_NET_WM_WINDOW_TYPE_DOCK) \
	X(_NET_WM_WINDOW_TYPE_MENU) \
	X(_NET_WM_WINDOW_TYPE_NOTIFICATION) \
	X(_NET_WM_WINDOW_TYPE_PROMPT) \
	X(_NET_WM_WINDOW_TYPE_SPLASH) \
	X(_NET_WM_WINDOW_TYPE_TOOLBAR) \
	X(_NET_WM_WINDOW_TYPE_UTILITY) \
	X(_MOTIF_WM_HINTS) \
	X(_GNUSTEP_WM_ATTR) \
	X(_SHOD_WM_STATE_STRETCHED) \
	X(_SHOD_CYCLE) \
	X(_SHOD_GROUP_TAB) \
	X(_SHOD_GROUP_CONTAINER) \
	X(_SHOD_CONTAINER_LIST) \
	X(_SHOD_DOCK_LIST)

enum Atom {
#define X(atom) atom,
	ATOMS
	NATOMS
#undef  X
};

int max(int x, int y);
int min(int x, int y);
void *ecalloc(size_t nmemb, size_t size);
void *emalloc(size_t size);
char *estrndup(const char *s, size_t maxlen);

long getprop(Display *display, Window window, Atom property, Atom type,
		int format, unsigned long length, void **data);
long getwinsprop(Display *display, Window window, Atom property, Window **data);
long getatomsprop(Display *display, Window window, Atom property, Atom **data);
long getcardsprop(Display *display, Window window, Atom property, long **data);
Window getwinprop(Display *display, Window window, Atom property);
Atom getatomprop(Display *display, Window window, Atom property);
long getcardprop(Display *display, Window window, Atom property);

Bool released_inside(Display *display, XButtonPressedEvent const *press);
Bool compress_motion(Display *, XEvent *);
