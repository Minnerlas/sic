# ii - irc it - simple but flexible IRC client
#   (C)opyright MMV Anselm R. Garbe, Nico Golde

include config.mk

SRC = sic.c
OBJ = ${SRC:.c=.o}
MAN1 = sic.1
BIN = sic

all: options sic
	@echo built sic

options:
	@echo ii build options:
	@echo "LIBS     = ${LIBS}"
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

clean:
	rm -f sic *.o core sic-${VERSION}.tar.gz

dist: clean
	@mkdir -p sic-${VERSION}
	@cp -R Makefile README LICENSE config.mk sic.c sic.1 sic-${VERSION}
	@tar -cf sic-${VERSION}.tar sic-${VERSION}
	@gzip sic-${VERSION}.tar
	@rm -rf sic-${VERSION}

sic: ${OBJ}
	@echo LD $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

install: all
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f ${BIN} ${DESTDIR}${PREFIX}/bin
	@for i in ${BIN}; do \
		chmod 755 ${DESTDIR}${PREFIX}/bin/`basename $$i`; \
	done
	@echo installed executable files to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@cp -f ${MAN1} ${DESTDIR}${MANPREFIX}/man1
	@for i in ${MAN1}; do \
		chmod 444 ${DESTDIR}${MANPREFIX}/man1/`basename $$i`; \
	done
	@echo installed manual pages to ${DESTDIR}${MANPREFIX}/man1

uninstall:
	for i in ${BIN}; do \
		rm -f ${DESTDIR}${PREFIX}/bin/`basename $$i`; \
	done
	for i in ${MAN1}; do \
		rm -f ${DESTDIR}${MANPREFIX}/man1/`basename $$i`; \
	done
