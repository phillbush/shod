# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib

# includes and libs
XCPPFLAGS = -I${LOCALINC} -I${X11INC} -I/usr/include/freetype2 -I${X11INC}/freetype2
XLDFLAGS  = -L${LOCALLIB} -L${X11LIB} -lfontconfig -lXft -lX11 -lXinerama -lXrender

SHOD_OBJS   = shod.o config.o \
              xapp.o xbar.o xdock.o xsplash.o xnotif.o xprompt.o \
              xhints.o xmon.o xdraw.o xevents.o
SHODC_OBJS  = shodc.o
SHARED_OBJS = xutil.o
PROGS = shod shodc
OBJS  = ${SHOD_OBJS} ${SHODC_OBJS} ${SHARED_OBJS}
INCS  = shod.h xutil.h
SRCS  = ${OBJS:.o=.c} ${INCS}

all: ${PROGS}

shod: ${SHOD_OBJS} ${SHARED_OBJS}
	${CC} -o $@ ${SHOD_OBJS} ${SHARED_OBJS} ${XLDFLAGS} ${LDFLAGS}

shodc: ${SHODC_OBJS} ${SHARED_OBJS}
	${CC} -o $@ ${SHODC_OBJS} ${SHARED_OBJS} ${XLDFLAGS} ${LDFLAGS}

${SHOD_OBJS}: shod.h xutil.h

${SHODC_OBJS}: xutil.h

${SHARED_OBJS}: xutil.h

.c.o:
	${CC} ${XCPPFLAGS} ${CFLAGS} ${CPPFLAGS} -c $<

tags: ${SRCS}
	ctags ${SRCS}

test: ${PROGS}
	xinit ${XINITRC} -- `which Xephyr` :1 -screen 1024x768 +xinerama

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	install -m 755 shod ${DESTDIR}${PREFIX}/bin/shod
	install -m 755 shodc ${DESTDIR}${PREFIX}/bin/shodc
	install -m 644 shod.1 ${DESTDIR}${MANPREFIX}/man1/shod.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/shod
	rm -f ${DESTDIR}${PREFIX}/bin/shodc
	rm -f ${DESTDIR}${MANPREFIX}/man1/shod.1

clean:
	rm -f ${PROGS} ${PROGS:=.core} ${OBJS} tags

.PHONY: all install uninstall clean
