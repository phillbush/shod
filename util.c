#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#include <X11/extensions/Xrender.h>

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

long
getprop(Display *display, Window window, Atom property, Atom type,
		int format, unsigned long length, void **data)
{
	unsigned long actual_length;
	int actual_format;
	Atom actual_type;

	*data = NULL;
	if (XGetWindowProperty(
		display, window, property, 0, INT_MAX, False, type,
		&actual_type, &actual_format, &actual_length,
		&(unsigned long){0}, (void *)data
	) != Success)
		goto error;
	if (data == NULL)
		goto error;
	if (type != AnyPropertyType && actual_type != type)
		goto error;
	if (length > 0 && actual_length != length)
		goto error;
	if (format != 0 && actual_format != format)
		goto error;
	if (actual_length == 0) {
		XFree(*data);
		*data = NULL;
	}
	return actual_length;
error:
	XFree(*data);
	*data = NULL;
	return -1;
}

long
getwinsprop(Display *display, Window window, Atom property, Window **data)
{
	return getprop(
		display, window, property,
		XA_WINDOW, 32, 0, (void *)data
	);
}

Window
getwinprop(Display *display, Window window, Atom property)
{
	Window *data = NULL;
	Window retval = None;

	if (getwinsprop(display, window, property, &data) == 1)
		retval = data[0];
	XFree(data);
	return retval;
}

long
getcardsprop(Display *display, Window window, Atom property, long **data)
{
	return getprop(
		display, window, property,
		XA_CARDINAL, 32, 0, (void *)data
	);
}

long
getcardprop(Display *display, Window window, Atom property)
{
	long *data = NULL;
	long retval = 0;

	if (getcardsprop(display, window, property, &data) == 1)
		retval = data[0];
	XFree(data);
	return retval;
}

long
getatomsprop(Display *display, Window window, Atom property, Atom **data)
{
	return getprop(
		display, window, property,
		XA_ATOM, 32, 0, (void *)data
	);
}

Atom
getatomprop(Display *display, Window window, Atom property)
{
	Atom *data = NULL;
	Atom retval = 0;

	if (getatomsprop(display, window, property, &data) == 1)
		retval = data[0];
	XFree(data);
	return retval;
}

Bool
compress_motion(Display *display, XEvent *event)
{
#define DELAY_MOUSE 32
	static Time last_motion = 0;
	XEvent next;

	if (event->type != MotionNotify)
		return False;
	while (XPending(display)) {
		XPeekEvent(display, &next);
		if (next.type != MotionNotify)
			break;
		if (next.xmotion.window != event->xmotion.window)
			break;
		if (next.xmotion.subwindow != event->xmotion.subwindow)
			break;
		XNextEvent(display, event);
	}
	if (event->xmotion.time - last_motion < DELAY_MOUSE)
		return False;
	last_motion = event->xmotion.time;
	return True;
}

void
window_configure_notify(Display *display, Window window, int x, int y, int w, int h)
{
	XSendEvent(
		display, window, False,
		StructureNotifyMask,
		(XEvent *)&(XConfigureEvent){
			.type = ConfigureNotify,
			.display = display,
			.x = x,
			.y = y,
			.width = w,
			.height = h,
			.border_width = 0,
			.above = None,
			.override_redirect = False,
			.event = window,
			.window = window,
		}
	);
}

Bool
released_inside(Display *display, XButtonPressedEvent const *press)
{
	XButtonReleasedEvent *release = (void *)(XEvent [1]){0};
	unsigned width, height;
	void *dummy = &(int){0};

	(void)XMaskEvent(display, ButtonReleaseMask, (void *)release);
	if (XGetGeometry(
		display, press->window, &(Window){0},
		dummy, dummy, &width, &height, dummy, dummy
	)) return (
		release->button == press->button &&
		release->x >= 0 && release->x < (int)width &&
		release->y >= 0 && release->y < (int)height
	);
	return False;
}
