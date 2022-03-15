# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man
LOCALINC = /usr/local/include
LOCALLIB = /usr/local/lib
X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib
FREETYPEINC = /usr/include/freetype2
OBSDFREETYPEINC = ${X11INC}/freetype2

# includes and libs
INCS = -I${LOCALINC} -I${X11INC} -I${FREETYPEINC} -I${OBSDFREETYPEINC}
LIBS = -L${LOCALLIB} -L${X11LIB} -lfontconfig -lXft -lX11 -lXinerama -lXrender

# flags
CFLAGS = -g -O0 -Wall -Wextra ${INCS} ${CPPFLAGS}
LDFLAGS = ${LIBS}

# compiler and linker
CC = cc

# files
PROGS = shod shodc
SRCS = ${PROGS:=.c}
OBJS = ${SRCS:.c=.o}

all: ${PROGS}

shod: shod.o
	${CC} -o $@ shod.o ${LDFLAGS}

shod.o: config.h

shodc: shodc.o
	${CC} -o $@ shodc.o ${LDFLAGS}

.c.o:
	${CC} ${CFLAGS} -c $<

install: all
	install -D -m 755 shod ${DESTDIR}${PREFIX}/bin/shod
	install -D -m 755 shodc ${DESTDIR}${PREFIX}/bin/shodc
	install -D -m 644 shod.1 ${DESTDIR}${MANPREFIX}/man1/shod.1

uninstall:
	rm -f ${DESTDIR}/${PREFIX}/bin/shod
	rm -f ${DESTDIR}/${PREFIX}/bin/shodc
	rm -f ${DESTDIR}/${MANPREFIX}/man1/shod.1

clean:
	-rm ${OBJS} ${PROGS}

.PHONY: all install uninstall clean
