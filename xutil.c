#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "xutil.h"

#include <X11/extensions/Xrender.h>

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

unsigned long
getcardprop(Window win, Atom prop)
{
	unsigned long *array;
	unsigned long card = 0;

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

char *
getwinname(Window win)
{
#define MAXLEN 512
	char *name = NULL;
	unsigned long length, remain;
	int format;
	Atom props[] = {atoms[_NET_WM_NAME], XA_WM_NAME};
	Atom type;

	for (size_t i = 0; i < LEN(props); i++) {
		if (XGetWindowProperty(
			dpy, win, atoms[_NET_WM_NAME],
			0L, 1024, False,
			AnyPropertyType, &type, &format,
			&length, &remain, (void*)&name
		) == Success && format == 8)
			return name;
		XFree(name);
	}
	return NULL;
}

void
settitle(Window win, const char *title)
{
	struct {
		Atom prop, type;
	} props[] = {
		{ atoms[_NET_WM_NAME],          atoms[UTF8_STRING] },
		{ atoms[_NET_WM_ICON_NAME],     atoms[UTF8_STRING] },
		{ XA_WM_NAME,                   XA_STRING },
		{ XA_WM_ICON_NAME,              XA_STRING },
	};
	size_t len, i;

	len = strlen(title);
	for (i = 0; i < LEN(props); i++) {
		XChangeProperty(
			dpy, win,
			props[i].prop,
			props[i].type,
			8,
			PropModeReplace,
			(unsigned char *)title,
			len
		);
	}
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
	int fd;

	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "could not open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	fd = XConnectionNumber(dpy);
	while (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
		if (errno == EINTR)
			continue;
		err(EXIT_FAILURE, "fcntl");
	}
}

Bool
compress_motion(XEvent *event)
{
#define DELAY_MOUSE 24
	static Time last_motion = 0;
	XEvent next;

	if (event->type != MotionNotify)
		return False;
	while (XPending(dpy)) {
		XPeekEvent(dpy, &next);
		if (next.type != MotionNotify)
			break;
		if (next.xmotion.window != event->xmotion.window)
			break;
		if (next.xmotion.subwindow != event->xmotion.subwindow)
			break;
		XNextEvent(dpy, event);
	}
	if (event->xmotion.time - last_motion < DELAY_MOUSE)
		return False;
	last_motion = event->xmotion.time;
	return True;
}
