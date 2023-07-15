#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "xutil.h"

#include <X11/extensions/Xrender.h>

Visual *visual;
Colormap colormap;
unsigned int depth;
XrmDatabase xdb = NULL;
Display *dpy;
Window root;
Atom atoms[NATOMS];
int screen;

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

char *
getresource(XrmDatabase xdb, XrmClass *class, XrmName *name)
{
	XrmRepresentation tmp;
	XrmValue xval;

	if (xdb == NULL)
		return NULL;
	if (XrmQGetResource(xdb, name, class, &tmp, &xval))
		return xval.addr;
	return NULL;
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
	static char *atomnames[NATOMS] = {
#define X(atom) [atom] = #atom,
		ATOMS
#undef  X
	};
	XInternAtoms(dpy, atomnames, NATOMS, False, atoms);
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
