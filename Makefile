# paths
PREFIX ?= /usr/local
MANPREFIX ?= ${PREFIX}/share/man
LOCALINC ?= /usr/local/include
LOCALLIB ?= /usr/local/lib
X11INC ?= /usr/X11R6/include
X11LIB ?= /usr/X11R6/lib
FREETYPEINC ?= /usr/include/freetype2
# OpenBSD (uncomment)
#FREETYPEINC = ${X11INC}/freetype2

# includes and libs
INCS += -I${LOCALINC} -I${X11INC} -I${FREETYPEINC}
LIBS += -L${LOCALLIB} -L${X11LIB} -lfontconfig -lXft -lX11 -lXinerama -lXrender

# files
PROGS = shod shodc
SRCS = ${PROGS:=.c}
OBJS = ${SRCS:.c=.o}

all: ${PROGS}

shod: shod.o
	${CC} -o $@ shod.o ${LIBS} ${LDFLAGS}

shod.o: config.h

shodc: shodc.o
	${CC} -o $@ shodc.o ${LIBS} ${LDFLAGS}

.c.o:
	${CC} ${INCS} ${CFLAGS} ${CPPFLAGS} -c $<

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	${INSTALL} -m 755 shod ${DESTDIR}${PREFIX}/bin/shod
	${INSTALL} -m 755 shodc ${DESTDIR}${PREFIX}/bin/shodc
	${INSTALL} -m 644 shod.1 ${DESTDIR}${MANPREFIX}/man1/shod.1

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/shod
	rm -f ${DESTDIR}${PREFIX}/bin/shodc
	rm -f ${DESTDIR}${MANPREFIX}/man1/shod.1

clean:
	-rm -f ${OBJS} ${PROGS} ${PROGS:=.core}

.PHONY: all install uninstall clean
