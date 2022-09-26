#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "xutil.h"

#include <X11/extensions/Xrender.h>

static char *atomnames[ATOM_LAST] = {
	[UTF8_STRING]                          = "UTF8_STRING",
	[WM_DELETE_WINDOW]                     = "WM_DELETE_WINDOW",
	[WM_WINDOW_ROLE]                       = "WM_WINDOW_ROLE",
	[WM_TAKE_FOCUS]                        = "WM_TAKE_FOCUS",
	[WM_PROTOCOLS]                         = "WM_PROTOCOLS",
	[WM_STATE]                             = "WM_STATE",
	[WM_CLIENT_LEADER]                     = "WM_CLIENT_LEADER",
	[_NET_SUPPORTED]                       = "_NET_SUPPORTED",
	[_NET_DESKTOP_NAMES]                   = "_NET_DESKTOP_NAMES",
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
	[_NET_WM_FULL_PLACEMENT]               = "_NET_WM_FULL_PLACEMENT",
	[_MOTIF_WM_HINTS]                      = "_MOTIF_WM_HINTS",
	[_SHOD_GROUP_TAB]                      = "_SHOD_GROUP_TAB",
	[_SHOD_GROUP_CONTAINER]                = "_SHOD_GROUP_CONTAINER",
};

Visual *visual;
Colormap colormap;
unsigned int depth;
XrmDatabase xdb;
Display *dpy;
Window root;
Atom atoms[ATOM_LAST];
int screen;
char *xrm;

int
max(int x, int y)
{
	return x > y ? x : y;
}

int
min(int x, int y)
{
	return x < y ? x : y;
}

void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;
	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

char *
estrndup(const char *s, size_t maxlen)
{
	char *p;

	if ((p = strndup(s, maxlen)) == NULL)
		err(1, "strndup");
	return p;
}

unsigned long
getwinsprop(Window win, Atom prop, Window **wins)
{
	unsigned char *list;
	unsigned long len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */

	list = NULL;
	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_WINDOW,
		               &da, &di, &len, &dl, &list) != Success || list == NULL) {
		*wins = NULL;
		return 0;
	}
	*wins = (Window *)list;
	return len;
}

Window
getwinprop(Window win, Atom prop)
{
	Window *wins;
	Window ret = None;

	getwinsprop(win, prop, &wins);
	if (wins != NULL)
		ret = *wins;
	XFree(wins);
	return ret;
}

unsigned long
getcardsprop(Window win, Atom prop, Atom **array)
{
	unsigned char *p;
	unsigned long len;
	unsigned long dl;
	int di;
	Atom da;

	p = NULL;
	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_CARDINAL, &da, &di, &len, &dl, &p) != Success || p == NULL) {
		*array = NULL;
		XFree(p);
		return 0;
	}
	*array = (Atom *)p;
	return len;
}

Atom
getcardprop(Window win, Atom prop)
{
	unsigned long *array;
	Atom card = None;

	getcardsprop(win, prop, &array);
	if (array != NULL)
		card = *array;
	XFree(array);
	return card;
}

unsigned long
getatomsprop(Window win, Atom prop, Atom **atoms)
{
	unsigned char *p;
	unsigned long len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */

	p = NULL;
	if (XGetWindowProperty(dpy, win, prop, 0L, 1024, False, XA_ATOM, &da, &di, &len, &dl, &p) != Success || p == NULL) {
		*atoms = NULL;
		XFree(p);
		return 0;
	}
	*atoms = (Atom *)p;
	return len;
}

Atom
getatomprop(Window win, Atom prop)
{
	Atom *atoms;
	Atom atom = None;

	getatomsprop(win, prop, &atoms);
	if (atoms != NULL)
		atom = *atoms;
	XFree(atoms);
	return atom;
}

void
initatoms(void)
{
	XInternAtoms(dpy, atomnames, ATOM_LAST, False, atoms);
}

void
initatom(int atomenum)
{
	atoms[atomenum] = XInternAtom(dpy, atomnames[atomenum], False);
}

void
xinit(void)
{
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
}

void
xinitvisual(void)
{
	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;
	int nitems;
	int i;

	visual = NULL;
	if ((infos = XGetVisualInfo(dpy, masks, &tpl, &nitems)) != NULL) {
		for (i = 0; i < nitems; i++) {
			fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
			if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
				depth = infos[i].depth;
				visual = infos[i].visual;
				colormap = XCreateColormap(dpy, root, visual, AllocNone);
				break;
			}
		}
		XFree(infos);
	}
	if (visual == NULL) {
		depth = DefaultDepth(dpy, screen);
		visual = DefaultVisual(dpy, screen);
		colormap = DefaultColormap(dpy, screen);
	}
}

void
xiniterrfunc(int (*xerror)(Display *, XErrorEvent *), int (**xerrorxlib)())
{
	*xerrorxlib = XSetErrorHandler(xerror);
}
