#include <sys/wait.h>

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shod.h"

#define WMNAME          "shod"

static int (*xerrorxlib)(Display *, XErrorEvent *);
volatile sig_atomic_t running = 1;

/* shared variables */
unsigned long clientmask = CWEventMask | CWColormap | CWBackPixel | CWBorderPixel;
XSetWindowAttributes clientswa = {
	.event_mask = SubstructureNotifyMask | ExposureMask
	            | SubstructureRedirectMask | ButtonPressMask | FocusChangeMask
};
struct WM wm = {};
struct Dock dock;

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: shod [-cdst] [-m modifier] [file]\n");
	exit(1);
}

/* call fork checking for error; exit on error */
static pid_t
efork(void)
{
	pid_t pid;

	if ((pid = fork()) < 0)
		err(1, "fork");
	return pid;
}

/* call execlp checking for error; exit on error */
static void
execshell(char *filename)
{
	char *argv[3];

	if ((argv[0] = getenv(SHELL)) == NULL)
		argv[0] = DEF_SHELL;
	if (filename[0] == '-' && filename[1] == '\0')
		argv[1] = NULL;         /* read commands from stdin */
	else
		argv[1] = filename;     /* read commands from file */
	argv[2] = NULL;
	if (execvp(argv[0], argv) == -1) {
		err(1, "%s", argv[0]);
	}
}

/* error handler */
static int
xerror(Display *dpy, XErrorEvent *e)
{
	/* stolen from berry, which stole from katriawm, which stole from dwm lol */

	/* There's no way to check accesses to destroyed windows, thus those
	 * cases are ignored (especially on UnmapNotify's). Other types of
	 * errors call Xlibs default error handler, which may call exit. */
	if (e->error_code == BadWindow ||
	    (e->request_code == X_SetInputFocus && e->error_code == BadMatch) ||
	    (e->request_code == X_PolyText8 && e->error_code == BadDrawable) ||
	    (e->request_code == X_PolyFillRectangle && e->error_code == BadDrawable) ||
	    (e->request_code == X_PolySegment && e->error_code == BadDrawable) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadMatch) ||
	    (e->request_code == X_ConfigureWindow && e->error_code == BadValue) ||
	    (e->request_code == X_GrabButton && e->error_code == BadAccess) ||
	    (e->request_code == X_GrabKey && e->error_code == BadAccess) ||
	    (e->request_code == X_CopyArea && e->error_code == BadDrawable) ||
	    (e->request_code == 139 && e->error_code == BadDrawable) ||
	    (e->request_code == 139 && e->error_code == 143))
		return 0;

	errx(1, "Fatal request. Request code=%d, error code=%d", e->request_code, e->error_code);
	return xerrorxlib(dpy, e);
}

/* stop running */
static void
siginthandler(int signo)
{
	(void)signo;
	running = 0;
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	long n;
	char *type;
	XrmValue xval;

	if (xrm == NULL || xdb == NULL)
		return;

	if (XrmGetResource(xdb, "shod.faceName", "*", &type, &xval) == True)
		config.font = xval.addr;
	if (XrmGetResource(xdb, "shod.foreground", "*", &type, &xval) == True)
		config.foreground = xval.addr;

	if (XrmGetResource(xdb, "shod.dockBackground", "*", &type, &xval) == True)
		config.dockcolors[COLOR_DEF] = xval.addr;
	if (XrmGetResource(xdb, "shod.dockBorder", "*", &type, &xval) == True)
		config.dockcolors[COLOR_ALT] = xval.addr;

	if (XrmGetResource(xdb, "shod.activeBackground", "*", &type, &xval) == True)
		config.bordercolors[FOCUSED][COLOR_MID] = xval.addr;
	if (XrmGetResource(xdb, "shod.activeTopShadowColor", "*", &type, &xval) == True)
		config.bordercolors[FOCUSED][COLOR_LIGHT] = xval.addr;
	if (XrmGetResource(xdb, "shod.activeBottomShadowColor", "*", &type, &xval) == True)
		config.bordercolors[FOCUSED][COLOR_DARK] = xval.addr;

	if (XrmGetResource(xdb, "shod.inactiveBackground", "*", &type, &xval) == True)
		config.bordercolors[UNFOCUSED][COLOR_MID] = xval.addr;
	if (XrmGetResource(xdb, "shod.inactiveTopShadowColor", "*", &type, &xval) == True)
		config.bordercolors[UNFOCUSED][COLOR_LIGHT] = xval.addr;
	if (XrmGetResource(xdb, "shod.inactiveBottomShadowColor", "*", &type, &xval) == True)
		config.bordercolors[UNFOCUSED][COLOR_DARK] = xval.addr;

	if (XrmGetResource(xdb, "shod.urgentBackground", "*", &type, &xval) == True)
		config.bordercolors[URGENT][COLOR_MID] = xval.addr;
	if (XrmGetResource(xdb, "shod.urgentTopShadowColor", "*", &type, &xval) == True)
		config.bordercolors[URGENT][COLOR_LIGHT] = xval.addr;
	if (XrmGetResource(xdb, "shod.urgentBottomShadowColor", "*", &type, &xval) == True)
		config.bordercolors[URGENT][COLOR_DARK] = xval.addr;

	if (XrmGetResource(xdb, "shod.borderWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.borderwidth = n;
	if (XrmGetResource(xdb, "shod.shadowThickness", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.shadowthickness = n;
	if (XrmGetResource(xdb, "shod.titleWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.titlewidth = n;
	if (XrmGetResource(xdb, "shod.dockWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.dockwidth = n;
	if (XrmGetResource(xdb, "shod.dockSpace", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.dockspace = n;
	if (XrmGetResource(xdb, "shod.dockGravity", "*", &type, &xval) == True)
		config.dockgravity = xval.addr;
	if (XrmGetResource(xdb, "shod.notifGap", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.notifgap = n;
	if (XrmGetResource(xdb, "shod.notifGravity", "*", &type, &xval) == True)
		config.notifgravity = xval.addr;
	if (XrmGetResource(xdb, "shod.numOfDesktops", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0 && n < 100)
			config.ndesktops = n;
	if (XrmGetResource(xdb, "shod.snapProximity", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) >= 0 && n < 100)
			config.snap = n;
}

/* read command-line options */
static char *
getoptions(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "cdm:st")) != -1) {
		switch (c) {
		case 'c':
			config.honorconfig = 1;
			break;
		case 'd':
			config.floatdialog = 1;
			break;
		case 'm':
			if ((config.altkeysym = XStringToKeysym(optarg)) == NoSymbol)
				errx(1, "supplied key does not match any key symbol: %s", optarg);
			break;
		case 's':
			config.sloppyfocus = 1;
			break;
		case 't':
			config.disablealttab = 1;
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
	return *argv;
}

/* initialize signals */
static void
initsignal(void)
{
	struct sigaction sa;

	/* remove zombies, we may inherit children when exec'ing shod in .xinitrc */
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGCHLD, &sa, NULL) == -1)
		err(1, "sigaction");

	/* set running to 0 */
	sa.sa_handler = siginthandler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGINT, &sa, NULL) == -1)
		err(1, "sigaction");
}

/* initialize cursors */
static void
initcursors(void)
{
	wm.cursors[CURSOR_NORMAL] = XCreateFontCursor(dpy, XC_left_ptr);
	wm.cursors[CURSOR_MOVE] = XCreateFontCursor(dpy, XC_fleur);
	wm.cursors[CURSOR_NW] = XCreateFontCursor(dpy, XC_top_left_corner);
	wm.cursors[CURSOR_NE] = XCreateFontCursor(dpy, XC_top_right_corner);
	wm.cursors[CURSOR_SW] = XCreateFontCursor(dpy, XC_bottom_left_corner);
	wm.cursors[CURSOR_SE] = XCreateFontCursor(dpy, XC_bottom_right_corner);
	wm.cursors[CURSOR_N] = XCreateFontCursor(dpy, XC_top_side);
	wm.cursors[CURSOR_S] = XCreateFontCursor(dpy, XC_bottom_side);
	wm.cursors[CURSOR_W] = XCreateFontCursor(dpy, XC_left_side);
	wm.cursors[CURSOR_E] = XCreateFontCursor(dpy, XC_right_side);
	wm.cursors[CURSOR_V] = XCreateFontCursor(dpy, XC_sb_v_double_arrow);
	wm.cursors[CURSOR_H] = XCreateFontCursor(dpy, XC_sb_h_double_arrow);
	wm.cursors[CURSOR_HAND] = XCreateFontCursor(dpy, XC_hand2);
	wm.cursors[CURSOR_PIRATE] = XCreateFontCursor(dpy, XC_pirate);
}

/* set up root window */
static void
initroot(void)
{
	XSetWindowAttributes swa;

	/* Select SubstructureRedirect events on root window */
	swa.cursor = wm.cursors[CURSOR_NORMAL];
	swa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
	               | StructureNotifyMask | ButtonPressMask;
	XChangeWindowAttributes(dpy, root, CWEventMask | CWCursor, &swa);

	/* Set focus to root window */
	XSetInputFocus(dpy, root, RevertToParent, CurrentTime);
}

/* create dock window */
static void
initdock(void)
{
	XSetWindowAttributes swa;

	TAILQ_INIT(&dock.dappq);
	dock.pix = None;
	swa.event_mask = SubstructureNotifyMask | SubstructureRedirectMask | ExposureMask;
	swa.background_pixel = BlackPixel(dpy, screen);
	swa.border_pixel = BlackPixel(dpy, screen);
	swa.colormap = colormap;
	dock.win = XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
		                     depth, InputOutput, visual, clientmask, &swa);
}

/* create dummy windows used for controlling focus and the layer of clients */
static void
initdummywindows(void)
{
	int i;

	for (i = 0; i < LAYER_LAST; i++) {
		wm.layerwins[i] = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
		XRaiseWindow(dpy, wm.layerwins[i]);
	}
	wm.wmcheckwin = XCreateWindow(
		dpy, root,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth),
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		0, depth, CopyFromParent, visual,
		clientmask | KeyPressMask,
		&clientswa
	);
	wm.wmcheckpix = XCreatePixmap(
		dpy, wm.wmcheckwin,
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		depth
	);
}

/* map and hide dummy windows */
static void
mapdummywins(void)
{
	XMapWindow(dpy, wm.wmcheckwin);
}

/* run stdin on sh */
static void
autostart(char *filename)
{
	pid_t pid;

	if (filename == NULL)
		return;
	if ((pid = efork()) == 0) {
		if (efork() == 0)
			execshell(filename);
		exit(0);
	}
	waitpid(pid, NULL, 0);
}

/* destroy dummy windows */
static void
cleandummywindows(void)
{
	int i;

	XFreePixmap(dpy, wm.wmcheckpix);
	XDestroyWindow(dpy, wm.wmcheckwin);
	for (i = 0; i < LAYER_LAST; i++) {
		XDestroyWindow(dpy, wm.layerwins[i]);
	}
}

/* free cursors */
static void
cleancursors(void)
{
	size_t i;

	for (i = 0; i < CURSOR_LAST; i++) {
		XFreeCursor(dpy, wm.cursors[i]);
	}
}

/* clean window manager structures */
static void
cleanwm(void)
{
	struct Monitor *mon;
	struct Object *obj;
	struct Container *c;

	while ((c = TAILQ_FIRST(&wm.focusq)) != NULL)
		containerdel(c);
	while ((obj = TAILQ_FIRST(&wm.notifq)) != NULL)
		(void)unmanagenotif(obj, 0);
	while ((obj = TAILQ_FIRST(&wm.barq)) != NULL)
		(void)unmanagebar(obj, 0);
	while ((obj = TAILQ_FIRST(&wm.splashq)) != NULL)
		(void)unmanagesplash(obj, 0);
	while ((obj = TAILQ_FIRST(&dock.dappq)) != NULL)
		(void)unmanagedockapp(obj, 0);
	while ((mon = TAILQ_FIRST(&wm.monq)) != NULL)
		mondel(mon);
	if (dock.pix != None)
		XFreePixmap(dpy, dock.pix);
	XDestroyWindow(dpy, dock.win);
}

/* shod window manager */
int
main(int argc, char *argv[])
{
	XEvent ev;
	char *filename, *wmname;

	if (!setlocale(LC_ALL, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	xinit();
	xinitvisual();
	xiniterrfunc(xerror, &xerrorxlib);
	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);
	clientswa.colormap = colormap;
	clientswa.border_pixel = BlackPixel(dpy, screen);
	clientswa.background_pixel = BlackPixel(dpy, screen);

	/* get configuration */
	if ((wmname = strrchr(argv[0], '/')) != NULL)
		wmname++;
	else if (argv[0] != NULL && argv[0][0] != '\0')
		wmname = argv[0];
	else
		wmname = WMNAME;
	getresources();
	filename = getoptions(argc, argv);

	/* check sloppy focus */
	if (config.sloppyfocus)
		clientswa.event_mask |= EnterWindowMask;

	/* initialize */
	initsignal();
	initcursors();
	initatoms();
	initroot();
	initdummywindows();
	inittheme();
	initdock();

	/* initialize queues */
	TAILQ_INIT(&wm.monq);
	TAILQ_INIT(&wm.barq);
	TAILQ_INIT(&wm.splashq);
	TAILQ_INIT(&wm.notifq);
	TAILQ_INIT(&wm.focusq);
	TAILQ_INIT(&wm.fullq);
	TAILQ_INIT(&wm.aboveq);
	TAILQ_INIT(&wm.centerq);
	TAILQ_INIT(&wm.belowq);

	/* set up list of monitors */
	monupdate();
	wm.selmon = TAILQ_FIRST(&wm.monq);

	/* initialize ewmh hints */
	ewmhinit(wmname);
	ewmhsetcurrentdesktop(0);
	ewmhsetshowingdesktop(0);
	ewmhsetclients();
	ewmhsetclientsstacking();
	ewmhsetactivewindow(None);

	/* run sh script */
	autostart(filename);

	/* scan windows */
	scan();
	mapdummywins();

	/* set modifier key and grab alt key */
	setmod();

	/* run main event loop */
	while (running && !XNextEvent(dpy, &ev))
		if (xevents[ev.type])
			(*xevents[ev.type])(&ev);

	/* clean up */
	cleandummywindows();
	cleancursors();
	cleantheme();
	cleanwm();

	/* clear ewmh hints */
	ewmhsetclients();
	ewmhsetclientsstacking();
	ewmhsetactivewindow(None);

	/* close connection to server */
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);

	return 0;
}
