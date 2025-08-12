SHOD_OBJS   = shod.o config.o draw.o \
              container.o menu.o bar.o dock.o splash.o notification.o prompt.o
SHODC_OBJS  = shodc.o
SHARED_OBJS = util.o
OBJS        = ${SHOD_OBJS} ${SHODC_OBJS} ${SHARED_OBJS}

SRCS        = ${OBJS:.o=.c}

MANS        = shod.1

PROGS       = shod shodc

PROG_CPPFLAGS = \
	-D_POSIX_C_SOURCE=202405L -D_BSD_SOURCE -D_GNU_SOURCE -D_DEFAULT_SOURCE \
	-I/usr{,/local,/X11R6}/include{,/freetype2} \
	${CPPFLAGS}

PROG_CFLAGS = -std=c99 ${PROG_CPPFLAGS} ${CFLAGS}

PROG_LDFLAGS = -L/usr{,/local,/X11R6}/lib ${LDLIBS} ${LDFLAGS}

DEBUG_FLAGS = -g -O0 -DDEBUG -Wall -Wextra ${DEBUG}

all: ${PROGS}

shod: ${SHOD_OBJS} ${SHARED_OBJS}
	${CC} -o $@ ${SHOD_OBJS} ${SHARED_OBJS} \
	${PROG_LDFLAGS} -lfontconfig -lXft -lXrandr -lXrender -lX11

shodc: ${SHODC_OBJS} ${SHARED_OBJS}
	${CC} -o $@ ${SHODC_OBJS} ${SHARED_OBJS} ${PROG_LDFLAGS} -lX11

${SHOD_OBJS}: shod.h
${OBJS}: util.h

.c.o:
	${CC} ${PROG_CFLAGS} -o $@ -c $<

debug:
	@${MAKE} ${MAKEFLAGS} CFLAGS+="${DEBUG_FLAGS}" all

tags: ${SRCS}
	ctags -d ${SRCS}

lint: ${SRCS}
	-mandoc -T lint -W warning ${MANS}
	scan-build @${MAKE} debug

test: debug
	xinit ./xinitrc -- `which Xephyr` :1 -screen 800x600 +xinerama

clean:
	rm -f ${PROGS} ${PROGS:=.core} ${OBJS} tags

.PHONY: all debug clean lint
