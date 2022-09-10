#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "xutil.h"

#define NAMEMAXLEN                      128
#define DIRECT_ACTION                   2
#define _SHOD_MOVERESIZE_RELATIVE       ((long)(1 << 16))

/* state action */
enum {
	REMOVE = 0,
	ADD    = 1,
	TOGGLE = 2
};

/* long list char positions */
enum {
	LIST_DIALOG,
	LIST_STICKY,
	LIST_MAXIMIZED,
	LIST_MINIMIZED,
	LIST_FULLSCREEN,
	LIST_SHADED,
	LIST_LAYER,
	LIST_URGENCY,
	LIST_ACTIVE,
	LIST_LAST
};

/* focus relative direction */
enum Direction {
	_SHOD_FOCUS_ABSOLUTE            = 0,
	_SHOD_FOCUS_LEFT_CONTAINER      = 1,
	_SHOD_FOCUS_RIGHT_CONTAINER     = 2,
	_SHOD_FOCUS_TOP_CONTAINER       = 3,
	_SHOD_FOCUS_BOTTOM_CONTAINER    = 4,
	_SHOD_FOCUS_PREVIOUS_CONTAINER  = 5,
	_SHOD_FOCUS_NEXT_CONTAINER      = 6,
	_SHOD_FOCUS_LEFT_WINDOW         = 7,
	_SHOD_FOCUS_RIGHT_WINDOW        = 8,
	_SHOD_FOCUS_TOP_WINDOW          = 9,
	_SHOD_FOCUS_BOTTOM_WINDOW       = 10,
	_SHOD_FOCUS_PREVIOUS_WINDOW     = 11,
	_SHOD_FOCUS_NEXT_WINDOW         = 12,
};

/* global variables */
static Window active;

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: shodc close [WIN_ID]\n");
	(void)fprintf(stderr, "       shodc desks\n");
	(void)fprintf(stderr, "       shodc focus [-clrtbpnLRTBPN] [WIN_ID]\n");
	(void)fprintf(stderr, "       shodc geom [-X|-x N] [-Y|-y N] [-W|-w N] [-H|-h N] [WIN_ID]\n");
	(void)fprintf(stderr, "       shodc goto [-m MON_ID] DESK_ID\n");
	(void)fprintf(stderr, "       shodc list [-ls] [WIN_ID]\n");
	(void)fprintf(stderr, "       shodc sendto [-m MON_ID] DESK_ID [WIN_ID]\n");
	(void)fprintf(stderr, "       shodc state [-ATR] [-abfMms] [WIN_ID]\n");
	exit(1);
}

/* send client message to root window */
static void
clientmsg(Window win, Atom atom, unsigned long d0, unsigned long d1, unsigned long d2, unsigned long d3, unsigned long d4)
{
	XEvent ev;
	long mask = SubstructureRedirectMask | SubstructureNotifyMask;

	ev.xclient.type = ClientMessage;
	ev.xclient.serial = 0;
	ev.xclient.send_event = True;
	ev.xclient.message_type = atom;
	ev.xclient.window = win;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = d0;
	ev.xclient.data.l[1] = d1;
	ev.xclient.data.l[2] = d2;
	ev.xclient.data.l[3] = d3;
	ev.xclient.data.l[4] = d4;
	if (!XSendEvent(dpy, root, False, mask, &ev)) {
		errx(1, "could not send event");
	}
}

/* get active window */
static Window
getactivewin(void)
{
	Window win;
	unsigned char *list;
	unsigned long len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */

	list = NULL;
	if (XGetWindowProperty(dpy, root, atoms[_NET_ACTIVE_WINDOW], 0L, 1024, False, XA_WINDOW,
		               &da, &di, &len, &dl, &list) != Success || list == NULL)
		return None;
	win = *(Window *)list;
	XFree(list);
	return win;
}

/* get window from string */
static Window
getwin(const char *s)
{
	return (Window)strtol(s, NULL, 0);
}

/* get array of windows */
static unsigned long
getwins(Window **wins, int sflag)
{
	if (sflag)
		return getwinsprop(root, atoms[_NET_CLIENT_LIST_STACKING], wins);
	else
		return getwinsprop(root, atoms[_NET_CLIENT_LIST], wins);
}

/* get array of desktop names, return size of array */
static unsigned long
getdesknames(char **desknames)
{
	unsigned char *str;
	unsigned long len;
	unsigned long dl;   /* dummy variable */
	int di;             /* dummy variable */
	Atom da;            /* dummy variable */


	if (XGetWindowProperty(dpy, root, atoms[_NET_DESKTOP_NAMES], 0, ~0, False,
	                       UTF8_STRING, &da, &di, &len, &dl, &str) ==
	                       Success && str) {
		*desknames = (char *)str;
	} else {
		*desknames = NULL;
		len = 0;
	}

	return len;
}

/* get window name */
char *
getwinname(Window win)
{
	XTextProperty tprop;
	char **list = NULL;
	char *name = NULL;
	unsigned char *p = NULL;
	unsigned long size, dl;
	int di;
	Atom da;

	if (XGetWindowProperty(dpy, win, atoms[_NET_WM_NAME], 0L, 8L, False, UTF8_STRING,
	                       &da, &di, &size, &dl, &p) == Success && p) {
		name = estrndup((char *)p, NAMEMAXLEN);
		XFree(p);
	} else if (XGetWMName(dpy, win, &tprop) &&
		   XmbTextPropertyToTextList(dpy, &tprop, &list, &di) == Success &&
		   di > 0 && list && *list) {
		name = estrndup(*list, NAMEMAXLEN);
		XFreeStringList(list);
		XFree(tprop.value);
	}
	return name;
}

/* close window */
static void
closewin(int argc, char *argv[])
{
	Window win;

	win = None;
	if (argc == 1)
		win = active;
	else if (argc == 2 && argv[1][0] != '-')
		win = getwin(argv[1]);
	else
		usage();
	clientmsg(win, atoms[_NET_CLOSE_WINDOW], CurrentTime, DIRECT_ACTION, 0, 0, 0);
}

/* focus window */
static void
focuswin(int argc, char *argv[])
{
	Window win;
	enum Direction d;
	int cycle, c;

	cycle = 0;
	d = _SHOD_FOCUS_ABSOLUTE;
	win = None;
	while ((c = getopt(argc, argv, "clrtbpnLRTBPN")) != -1) {
		switch (c) {
		case 'c':
			cycle = 1;
			break;
		case 'L':
			d = _SHOD_FOCUS_LEFT_WINDOW;
			break;
		case 'R':
			d = _SHOD_FOCUS_RIGHT_WINDOW;
			break;
		case 'T':
			d = _SHOD_FOCUS_TOP_WINDOW;
			break;
		case 'B':
			d = _SHOD_FOCUS_BOTTOM_WINDOW;
			break;
		case 'P':
			d = _SHOD_FOCUS_PREVIOUS_WINDOW;
			break;
		case 'N':
			d = _SHOD_FOCUS_NEXT_WINDOW;
			break;
		case 'l':
			d = _SHOD_FOCUS_LEFT_CONTAINER;
			break;
		case 'r':
			d = _SHOD_FOCUS_RIGHT_CONTAINER;
			break;
		case 't':
			d = _SHOD_FOCUS_TOP_CONTAINER;
			break;
		case 'b':
			d = _SHOD_FOCUS_BOTTOM_CONTAINER;
			break;
		case 'p':
			d = _SHOD_FOCUS_PREVIOUS_CONTAINER;
			break;
		case 'n':
			d = _SHOD_FOCUS_NEXT_CONTAINER;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	if (argc == 1)
		win = getwin(argv[0]);
	clientmsg(win, atoms[_NET_ACTIVE_WINDOW], DIRECT_ACTION, CurrentTime, 0, d, cycle);
}

/* set container geometry */
static void
setgeom(int argc, char *argv[])
{
	Window win;
	long x, y, w, h;
	int rel;
	int c;

	rel = 0;
	x = y = w = h = 0;
	while ((c = getopt(argc, argv, "rx:y:w:h:")) != -1) {
		switch (c) {
		case 'r':
			rel |= _SHOD_MOVERESIZE_RELATIVE;
			break;
		case 'x':
			x = strtol(optarg, NULL, 10);
			break;
		case 'y':
			y = strtol(optarg, NULL, 10);
			break;
		case 'w':
			w = strtol(optarg, NULL, 10);
			break;
		case 'h':
			h = strtol(optarg, NULL, 10);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	win = (argc == 1) ? getwin(argv[0]) : active;
	clientmsg(win, atoms[_NET_MOVERESIZE_WINDOW], rel, x, y, w, h);
}

/* go to desktop */
static void
gotodesk(int argc, char *argv[])
{
	long mon, desk;
	int c;

	mon = 0;
	while ((c = getopt(argc, argv, "m")) != -1) {
		switch (c) {
		case 'm':
			mon = strtol(optarg, NULL, 0);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();
	desk = strtol(argv[0], NULL, 0) - 1;
	clientmsg(None, atoms[_NET_CURRENT_DESKTOP], desk, CurrentTime, mon, 0, 0);
}

/* list window type, states, properties, etc */
static void
longlist(Window win)
{
	Atom *as;
	int x, y;
	unsigned int w, h, b, du;
	int desk;
	unsigned long i, natoms, l;
	char state[] = "---------";
	char *name;
	XWMHints *wmhints = NULL;
	Window *list = NULL;
	Window dw;
	XID container, tab;

	container = tab = 0x0;
	if ((wmhints = XGetWMHints(dpy, win)) != NULL) {
		if (wmhints->flags & XUrgencyHint)
			state[LIST_URGENCY] = 'u';
		XFree(wmhints);
	}
	if (getwinsprop(win, XA_WM_TRANSIENT_FOR, &list) > 0) {
		if (*list != None) {
			state[LIST_DIALOG] = 'd';
		}
		XFree(list);
	}
	if (getwinsprop(win, atoms[_SHOD_GROUP_CONTAINER], &list) > 0) {
		if (*list != None) {
			container = *list;
		}
		XFree(list);
	}
	if (getwinsprop(win, atoms[_SHOD_GROUP_TAB], &list) > 0) {
		if (*list != None) {
			tab = *list;
		}
		XFree(list);
	}
	if (win == active)
		state[LIST_ACTIVE] = 'a';
	if ((natoms = getatomsprop(win, atoms[_NET_WM_STATE], &as)) > 0) {
		for (i = 0; i < natoms; i++) {
			if (as[i] == atoms[_NET_WM_STATE_STICKY]) {
				state[LIST_STICKY] = 'y';
			} else if (as[i] == atoms[_NET_WM_STATE_MAXIMIZED_VERT]) {
				if (state[LIST_MAXIMIZED] == 'h') {
					state[LIST_MAXIMIZED] = 'M';
				} else {
					state[LIST_MAXIMIZED] = 'v';
				}
			} else if (as[i] == atoms[_NET_WM_STATE_MAXIMIZED_HORZ]) {
				if (state[LIST_MAXIMIZED] == 'v') {
					state[LIST_MAXIMIZED] = 'M';
				} else {
					state[LIST_MAXIMIZED] = 'h';
				}
			} else if (as[i] == atoms[_NET_WM_STATE_HIDDEN]) {
				state[LIST_MINIMIZED] = 'm';
			} else if (as[i] == atoms[_NET_WM_STATE_SHADED]) {
				state[LIST_SHADED] = 's';
			} else if (as[i] == atoms[_NET_WM_STATE_FULLSCREEN]) {
				state[LIST_FULLSCREEN] = 'F';
			} else if (as[i] == atoms[_NET_WM_STATE_ABOVE]) {
				state[LIST_LAYER] = 'a';
			} else if (as[i] == atoms[_NET_WM_STATE_BELOW]) {
				state[LIST_LAYER] = 'b';
			} else if (as[i] == atoms[_NET_WM_STATE_DEMANDS_ATTENTION]) {
				if (state[LIST_URGENCY] == 'u') {
					state[LIST_URGENCY] = 'U';
				} else {
					state[LIST_URGENCY] = 'a';
				}
			} else if (as[i] == atoms[_NET_WM_STATE_FOCUSED]) {
				if (state[LIST_ACTIVE] == 'a') {
					state[LIST_ACTIVE] = 'A';
				} else {
					state[LIST_ACTIVE] = 'f';
				}
			}
		}
		XFree(as);
	}
	l = getcardprop(win, atoms[_NET_WM_DESKTOP]);
	desk = (l ==  0xFFFFFFFF) ? -1 : l;

	name = getwinname(win);
	XGetGeometry(dpy, win, &dw, &x, &y, &w, &h, &b, &du);
	XTranslateCoordinates(dpy, win, root, x, y, &x, &y, &dw);

	printf("%s\t%d\t%dx%d%+d%+d\t0x%08lx\t0x%08lx\t0x%08lx\t%s\n", state, desk, w, h, x, y, container, tab, win, name);
	free(name);
}

/* list desktops */
static void
listdesks(int argc, char *argv[])
{
	unsigned long i, nwins, desk, ndesks, curdesk, len, nameslen;
	unsigned long *wdesk;
	int *urgdesks;
	Window *wins;
	XWMHints *hints;
	char *desknames;

	(void)argv;
	if (argc != 1)
		usage();
	
	/* get variables */
	ndesks = getcardprop(root, atoms[_NET_NUMBER_OF_DESKTOPS]);
	curdesk = getcardprop(root, atoms[_NET_CURRENT_DESKTOP]);
	nameslen = getdesknames(&desknames);
	wdesk = ecalloc(ndesks, sizeof *wdesk);
	urgdesks = ecalloc(ndesks, sizeof *urgdesks);
	nwins = getwinsprop(root, atoms[_NET_CLIENT_LIST], &wins);
	for (i = 0; i < nwins; i++) {
		desk = getcardprop(wins[i], atoms[_NET_WM_DESKTOP]);
		hints = XGetWMHints(dpy, wins[i]);
		if (desk < ndesks) {
			wdesk[desk]++;
			if (hints && hints->flags & XUrgencyHint) {
				urgdesks[desk] = 1;
			}
		}
		XFree(hints);
	}
	XFree(wins);

	/* list desktops */
	for (len = i = 0; i < ndesks; i++) {
		printf("%c%lu:%s\n",
		       (i == curdesk ? '*' : (urgdesks[i] ? '-' : ' ')),
		       wdesk[i],
		       (desknames && len < nameslen ? desknames+len : ""));
		if (desknames && len < nameslen)
			len += strlen(desknames + len) + 1;
	}
	XFree(desknames);
	free(wdesk);
}

/* list windows */
static void
list(int argc, char *argv[])
{
	Window *wins;
	unsigned long nwins, i;
	int lflag, sflag;
	int c;

	lflag = sflag = 0;
	while ((c = getopt(argc, argv, "dls")) != -1) {
		switch (c) {
		case 'l':
			lflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	nwins = getwins(&wins, sflag);
	for (i = 0; i < nwins; i++) {
		if (lflag) {
			longlist(wins[i]);
		} else {
			printf("0x%08lx\n", wins[i]);
		}
	}
	XFree(wins);
}

/* send container to desktop */
static void
sendto(int argc, char *argv[])
{
	Window win;
	long mon, desk;
	int c;

	mon = 0;
	while ((c = getopt(argc, argv, "m")) != -1) {
		switch (c) {
		case 'm':
			mon = strtol(optarg, NULL, 0);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 2)
		usage();
	desk = strtol(argv[0], NULL, 0) - 1;
	win = (argc == 2) ? getwin(argv[1]) : active;
	clientmsg(win, atoms[_NET_WM_DESKTOP], desk, DIRECT_ACTION, mon, 0, 0);
}

/* set container state */
static void
state(int argc, char *argv[])
{
	Window win;
	Atom state1, state2;
	int action;
	int c;

	action = TOGGLE;
	state1 = state2 = None;
	while ((c = getopt(argc, argv, "ATRabfMmsy")) != -1) {
		switch (c) {
		case 'A':
			action = ADD;
			break;
		case 'T':
			action = TOGGLE;
			break;
		case 'R':
			action = REMOVE;
			break;
		case 'a':
			state1 = atoms[_NET_WM_STATE_ABOVE];
			state2 = None;
			break;
		case 'b':
			state1 = atoms[_NET_WM_STATE_BELOW];
			state2 = None;
			break;
		case 'f':
			state1 = atoms[_NET_WM_STATE_FULLSCREEN];
			state2 = None;
			break;
		case 'M':
			state1 = atoms[_NET_WM_STATE_MAXIMIZED_VERT];
			state2 = atoms[_NET_WM_STATE_MAXIMIZED_HORZ];
			break;
		case 'm':
			state1 = atoms[_NET_WM_STATE_HIDDEN];
			state2 = None;
			break;
		case 's':
			state1 = atoms[_NET_WM_STATE_SHADED];
			state2 = None;
			break;
		case 'y':
			state1 = atoms[_NET_WM_STATE_STICKY];
			state2 = None;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	if (state1 == None)
		return;
	win = (argc == 1) ? getwin(argv[0]) : active;
	clientmsg(win, atoms[_NET_WM_STATE], action, state1, state2, DIRECT_ACTION, 0);
}

/* shodc: remote controller for shod */
int
main(int argc, char *argv[])
{
	if (argc < 2)
		usage();

	xinit();
	initatoms();
	active = getactivewin();
	if (strcmp(argv[1], "close") == 0)
		closewin(argc - 1, argv + 1);
	else if (strcmp(argv[1], "desks") == 0)
		listdesks(argc - 1, argv + 1);
	else if (strcmp(argv[1], "focus") == 0)
		focuswin(argc - 1, argv + 1);
	else if (strcmp(argv[1], "geom") == 0)
		setgeom(argc - 1, argv + 1);
	else if (strcmp(argv[1], "goto") == 0)
		gotodesk(argc - 1, argv + 1);
	else if (strcmp(argv[1], "list") == 0)
		list(argc - 1, argv + 1);
	else if (strcmp(argv[1], "sendto") == 0)
		sendto(argc - 1, argv + 1);
	else if (strcmp(argv[1], "state") == 0)
		state(argc - 1, argv + 1);
	else
		usage();

	XCloseDisplay(dpy);
	return 0;
}
