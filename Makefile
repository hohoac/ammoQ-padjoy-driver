
VERSION = 0
BUILD = 8
GTKVER = "gtk+-2.0"
PLUGIN = libpadJoy-${VERSION}.${BUILD}.so
CFGPRG = cfgPadJoy
CFLAGS = -fPIC -Wall -O2 -fomit-frame-pointer -D_REENTRANT
OBJECTS = pad.o
CFGOBJ = cfg.o
LIBS = $(shell pkg-config ${GTKVER} --libs)
CFLAGS += $(shell pkg-config ${GTKVER} --cflags) -DVERSION=${VERSION} -DBUILD=${BUILD}

all: plugin config

plugin: ${OBJECTS}
	rm -f ${PLUGIN}
	gcc -shared -Wl,-soname,${PLUGIN} ${CFLAGS} ${OBJECTS} -o ${PLUGIN} -lpthread
	strip --strip-unneeded --strip-debug ${PLUGIN}

config: ${CFGOBJ}
	rm -f ${CFGPRG}
	gcc ${CFLAGS} ${CFGOBJ} -o ${CFGPRG} ${LIBS}
	strip --strip-unneeded --strip-debug ${CFGPRG}

clean:
	rm -f *.o *.so


# Dependencies

