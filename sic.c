 /* See LICENSE file for license details. */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *host = "irc.oftc.net";
static char *port = "ircd";
static char *password;
static char nick[32];
static char bufin[4096];
static char bufout[4096];
static char channel[256];
static time_t trespond;
static FILE *srv;

#include "util.c"

static void
pout(char *channel, char *fmt, ...) {
	static char timestr[18];
	time_t t;
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, sizeof bufout, fmt, ap);
	va_end(ap);
	t = time(NULL);
	strftime(timestr, sizeof timestr, "%D %R", localtime(&t));
	fprintf(stdout, "%-12s: %s %s\n", channel, timestr, bufout);
}

static void
sout(char *fmt, ...) {
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(bufout, sizeof bufout, fmt, ap);
	va_end(ap);
	fprintf(srv, "%s\r\n", bufout);
}

static void
privmsg(char *channel, char *msg) {
	if(channel[0] == '\0') {
		pout("", "No channel to send to");
		return;
	}
	pout(channel, "<%s> %s", nick, msg);
	sout("PRIVMSG %s :%s", channel, msg);
}

static void
parsein(char *msg) {
	char *p;
	char c;

	if(msg[0] == '\0')
		return;
	msg = ctok(&msg, '\n');
	if(msg[0] != ':') {
		privmsg(channel, msg);
		return;
	}
	c = *++msg;
	if(!c || !isspace(msg[1]))
		sout("%s", msg);
	else {
		if(msg[1])
			msg += 2;
		switch(c) {
		case 'j':
			sout("JOIN %s", msg);
			if(channel[0] == '\0')
				strlcpy(channel, msg, sizeof channel);
			break;
		case 'l':
			p = tok(&msg);
			if(!*p)
				p = channel;
			if(!*msg)
				msg = "sic - 250 LOC are too much!";
			sout("PART %s :%s", p, msg);
			break;
		case 'm':
			p = tok(&msg);
			privmsg(p, msg);
			break;
		case 's':
			strlcpy(channel, msg, sizeof channel);
			break;
		default:
			sout("%c %s", c, msg);
			break;
		}
	}
}

static void
parsesrv(char *msg) {
	char *cmd, *p, *usr, *txt;

	usr = host;
	if(!msg || !*msg)
		return;
	if(msg[0] == ':') {
		msg++;
		p = tok(&msg);
		if(!*msg)
			return;
		usr = ctok(&p, '!');
	}
	txt = ctok(&msg, '\r');
	msg = ctok(&txt, ':');
	cmd = tok(&msg);
	if(!strcmp("PONG", cmd))
		return;
	if(!strcmp("PRIVMSG", cmd))
		pout(msg, "<%s> %s", usr, txt);
	else if(!strcmp("PING", cmd))
		sout("PONG %s", txt);
	else {
		pout(usr, ">< %s: %s", cmd, txt);
		if(!strcmp("NICK", cmd) && !strcmp(usr, nick))
			strlcpy(nick, txt, sizeof nick);
	}
}

int
main(int argc, char *argv[]) {
	int i, c;
	struct timeval tv;
	const char *user = getenv("USER");
	fd_set rd;

	strlcpy(nick, user ? user : "unknown", sizeof nick);
	for(i = 1; i < argc; i++) {
		c = argv[i][1];
		if(argv[i][0] != '-' || argv[i][2])
			c = -1;
		switch(c) {
		case 'h':
			if(++i < argc) host = argv[i];
			break;
		case 'p':
			if(++i < argc) port = argv[i];
			break;
		case 'n':
			if(++i < argc) strlcpy(nick, argv[i], sizeof nick);
			break;
		case 'k':
			if(++i < argc) password = argv[i];
			break;
		case 'v':
			eprint("sic-"VERSION", © 2005-2009 Kris Maglione, Anselm R. Garbe, Nico Golde\n");
		default:
			eprint("usage: sic [-h host] [-p port] [-n nick] [-k keyword] [-v]\n");
		}
	}
	/* init */
	i = dial(host, port);
	srv = fdopen(i, "r+");
	/* login */
	if(password)
		sout("PASS %s", password);
	sout("NICK %s", nick);
	sout("USER %s localhost %s :%s", nick, host, nick);
	fflush(srv);
	setbuf(stdout, NULL);
	setbuf(srv, NULL);
	for(;;) { /* main loop */
		FD_ZERO(&rd);
		FD_SET(0, &rd);
		FD_SET(fileno(srv), &rd);
		tv.tv_sec = 120;
		tv.tv_usec = 0;
		i = select(fileno(srv) + 1, &rd, 0, 0, &tv);
		if(i < 0) {
			if(errno == EINTR)
				continue;
			eprint("sic: error on select():");
		}
		else if(i == 0) {
			if(time(NULL) - trespond >= 300)
				eprint("sic shutting down: parse timeout\n");
			sout("PING %s", host);
			continue;
		}
		if(FD_ISSET(fileno(srv), &rd)) {
			if(fgets(bufin, sizeof bufin, srv) == NULL)
				eprint("sic: remote host closed connection\n");
			parsesrv(bufin);
			trespond = time(NULL);
		}
		if(FD_ISSET(0, &rd)) {
			if(fgets(bufin, sizeof bufin, stdin) == NULL)
				eprint("sic: broken pipe\n");
			parsein(bufin);
		}
	}
	return 0;
}
