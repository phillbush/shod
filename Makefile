SHOD_OBJS   = shod.o config.o xhints.o xmon.o xdraw.o xevents.o \
              xcontainer.o xmenu.o xbar.o xdock.o xsplash.o xnotif.o xprompt.o
SHODC_OBJS  = shodc.o
SHARED_OBJS = xutil.o
PROGS = shod shodc
OBJS  = ${SHOD_OBJS} ${SHODC_OBJS} ${SHARED_OBJS}
SRCS  = ${OBJS:.o=.c}
MAN   = shod.1

PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib

# includes and libs
DEFS = -D_POSIX_C_SOURCE=200809L -DGNU_SOURCE -D_BSD_SOURCE
INCS = -I${LOCALINC} -I${X11INC} -I/usr/include/freetype2 -I${X11INC}/freetype2
LIBS  = -L${LOCALLIB} -L${X11LIB} -lfontconfig -lXft -lX11 -lXrandr -lXrender

bindir = ${DESTDIR}${PREFIX}/bin
mandir = ${DESTDIR}${MANPREFIX}/man1

all: ${PROGS}

shod: ${SHOD_OBJS} ${SHARED_OBJS}
	${CC} -o $@ ${SHOD_OBJS} ${SHARED_OBJS} ${LIBS} ${LDFLAGS}

shodc: ${SHODC_OBJS} ${SHARED_OBJS}
	${CC} -o $@ ${SHODC_OBJS} ${SHARED_OBJS} ${LIBS} ${LDFLAGS}

${SHOD_OBJS}: shod.h xutil.h

${SHODC_OBJS}: xutil.h

${SHARED_OBJS}: xutil.h

.c.o:
	${CC} -std=c99 -pedantic ${DEFS} ${INCS} ${CFLAGS} ${CPPFLAGS} -o $@ -c $<

tags: ${SRCS}
	ctags ${SRCS}

lint: ${SRCS}
	-mandoc -T lint -W warning ${MAN}
	-clang-tidy ${SRCS} -- -std=c99 ${DEFS} ${INCS} ${CPPFLAGS}

test: ${PROGS}
	xinit ${XINITRC} -- `which Xephyr` :1 -screen 1024x768 +xinerama

install: all
	mkdir -p ${bindir}
	mkdir -p ${mandir}
	for file in ${PROGS} ; do install -m 755 "$$file" ${bindir}/"$$file" ; done
	install -m 644 ${MAN} ${mandir}/${MAN}

uninstall:
	-for file in ${PROGS} ; do rm ${bindir}/"$$file" ; done
	-rm ${mandir}/${MAN}

clean:
	rm -f ${PROGS} ${PROGS:=.core} ${OBJS} tags

.PHONY: all clean install uninstall lint
