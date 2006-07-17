# Customize to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

VERSION = 0.0

# includes and libs
LIBS = -L${PREFIX}/lib -L/usr/lib -lc

# compiler
CFLAGS      = -Os -I${PREFIX}/include -I/usr/include \
			-DVERSION=\"${VERSION}\"
LDFLAGS     = ${LIBS}
#CFLAGS      = -g -Wall -O2 -I${PREFIX}/include -I/usr/include \
#			-DVERSION=\"${VERSION}\"
#LDFLAGS     = -g ${LIBS}
