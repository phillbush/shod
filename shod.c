#include <sys/wait.h>

#include <err.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/extensions/Xrandr.h>

#include "shod.h"

#define WMNAME          "shod"
#define ROOT_EVENTS     (SubstructureRedirectMask | SubstructureNotifyMask \
                        | StructureNotifyMask | ButtonPressMask \
                        | PropertyChangeMask)

static int (*xerrorxlib)(Display *, XErrorEvent *);
volatile sig_atomic_t running = 1;

/* shared variables */
unsigned long clientmask = CWEventMask | CWColormap | CWBackPixel | CWBorderPixel;
XSetWindowAttributes clientswa = {
	.event_mask = SubstructureNotifyMask | ExposureMask | ButtonReleaseMask
	            | SubstructureRedirectMask | ButtonPressMask | FocusChangeMask
	            | Button1MotionMask
};
struct WM wm = {};
struct Dock dock;

/* show usage and exit */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: shod [-cdhs] [-m modifier] [file]\n");
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

	fprintf(stderr, "shod: ");
	return xerrorxlib(dpy, e);
	exit(1);        /* unreached */
}

/* startup error handler to check if another window manager is already running. */
static int
xerrorstart(Display *dpy, XErrorEvent *e)
{
	(void)dpy;
	(void)e;
	errx(1, "another window manager is already running");
}

/* stop running */
static void
siginthandler(int signo)
{
	(void)signo;
	running = 0;
}

/* read command-line options */
static char *
getoptions(int argc, char *argv[])
{
	int c;

	while ((c = getopt(argc, argv, "cdhm:s")) != -1) {
		switch (c) {
		case 'c':
			config.honorconfig = 1;
			break;
		case 'd':
			config.floatdialog = 1;
			break;
		case 'h':
			config.disablehidden = 1;
			break;
		case 'm':
			if ((config.altkeysym = XStringToKeysym(optarg)) == NoSymbol)
				errx(1, "supplied key does not match any key symbol: %s", optarg);
			break;
		case 's':
			config.sloppyfocus = 1;
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
	/* change default cursor */
	XDefineCursor(dpy, root, wm.cursors[CURSOR_NORMAL]);

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
	XSetWindowAttributes swa;

	for (i = 0; i < LAYER_LAST; i++) {
		wm.layers[i].ncols = 0;
		wm.layers[i].frame = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
		XRaiseWindow(dpy, wm.layers[i].frame);
		TAILQ_INSERT_HEAD(&wm.stackq, &wm.layers[i], raiseentry);
	}
	swa = clientswa;
	swa.event_mask |= KeyPressMask;
	wm.wmcheckwin = XCreateWindow(
		dpy, root,
		- (2 * config.borderwidth + config.titlewidth),
		- (2 * config.borderwidth + config.titlewidth),
		2 * config.borderwidth + config.titlewidth,
		2 * config.borderwidth + config.titlewidth,
		0, depth, CopyFromParent, visual,
		clientmask,
		&swa
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
	XSetInputFocus(dpy, wm.wmcheckwin, RevertToParent, CurrentTime);
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

static void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	XSelectInput(dpy, root, ROOT_EVENTS);
	XSync(dpy, False);
	(void)XSetErrorHandler(xerror);
	XSync(dpy, False);
}

/* destroy dummy windows */
static void
cleandummywindows(void)
{
	int i;

	XFreePixmap(dpy, wm.wmcheckpix);
	XDestroyWindow(dpy, wm.wmcheckwin);
	for (i = 0; i < LAYER_LAST; i++) {
		XDestroyWindow(dpy, wm.layers[i].frame);
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
	char *filename, *wmname, *xrm;

	if (!setlocale(LC_ALL, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	xinit();
	checkotherwm();
	moninit();
	xinitvisual();
	XrmInitialize();
	xrm = XResourceManagerString(dpy);
	setresources(xrm);
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
	filename = getoptions(argc, argv);

	/* check sloppy focus */
	if (config.sloppyfocus)
		clientswa.event_mask |= EnterWindowMask;

	/* initialize queues */
	TAILQ_INIT(&wm.monq);
	TAILQ_INIT(&wm.barq);
	TAILQ_INIT(&wm.splashq);
	TAILQ_INIT(&wm.notifq);
	TAILQ_INIT(&wm.focusq);
	TAILQ_INIT(&wm.stackq);

	/* initialize */
	initsignal();
	initcursors();
	initatoms();
	initroot();
	initdummywindows();
	inittheme();
	initdock();

	/* set up list of monitors */
	monupdate();
	wm.selmon = TAILQ_FIRST(&wm.monq);

	/* initialize ewmh hints */
	ewmhinit(wmname);
	ewmhsetcurrentdesktop(0);
	ewmhsetshowingdesktop(0);
	ewmhsetclients();
	ewmhsetactivewindow(None);

	/* run sh script */
	autostart(filename);

	/* scan windows */
	scan();
	mapdummywins();

	/* set modifier key and grab alt key */
	setmod();

	/* run main event loop */
	while (running && !XNextEvent(dpy, &ev)) {
		wm.setclientlist = 0;
		if (wm.xrandr && ev.type - wm.xrandrev == RRScreenChangeNotify)
			monevent(&ev);
		else if (ev.type < LASTEvent && xevents[ev.type] != NULL)
			(*xevents[ev.type])(&ev);
		if (wm.setclientlist) {
			ewmhsetclients();
		}
	}

	/* clean up */
	cleandummywindows();
	cleancursors();
	cleantheme();
	cleanwm();

	/* clear ewmh hints */
	ewmhsetclients();
	ewmhsetactivewindow(None);

	/* close connection to server */
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XCloseDisplay(dpy);

	return 0;
}
