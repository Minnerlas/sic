 /* See LICENSE file for license details. */
#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <varargs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "arg.h"
#include "config.h"

extern char *getenv();

char *argv0;
static char *host = DEFAULT_HOST;
static char *port = DEFAULT_PORT;
static char *password;
static char nick[32];
static char bufin[4096];
static char bufout[4096];
static char channel[256];
static time_t trespond;
static FILE *srv;

#undef strlcpy
#include "strlcpy.c"
#include "util.c"

static int
isspac(a) {
	return isspace(a);
}

static void
pout(channel, fmt, va_alist)
char *channel;
char *fmt;
va_dcl
{
	static char timestr[80];
	time_t t;
	va_list ap;

	char *tmp, *tmp1;
	struct tm *tm;
	struct timeval tv;
	

	va_start(ap);
	vsprintf(bufout, fmt, ap);
	va_end(ap);
	t = time(NULL);
	/* strftime(timestr, sizeof timestr, TIMESTAMP_FORMAT, localtime(&t));*/
	/* fprintf(stdout, "%-12s: %s %s\n", channel, timestr, bufout); */

	gettimeofday(&tv,(struct timezone *)0);
	tm = localtime((time_t *)&tv.tv_sec);
	tmp = asctime(tm);
	tmp1 = strchr(tmp, '\n');
	if (*tmp1 == '\n') *tmp1 = '\0';

	fprintf(stdout, "%-12s: %s %s\n", channel, tmp, bufout);
}

static void
sout(fmt, va_alist)
char *fmt;
va_dcl
{
	va_list ap;

	va_start(ap);
	vsprintf(bufout, fmt, ap);
	va_end(ap);
	fprintf(srv, "%s\r\n", bufout);
}

static void
privmsg(channel, msg)
char *channel; char *msg;
{
	if(channel[0] == '\0') {
		pout("", "No channel to send to");
		return;
	}
	pout(channel, "<%s> %s", nick, msg);
	sout("PRIVMSG %s :%s", channel, msg);
}

static void
parsein(s)
char *s;
{
	char c, *p;

	if(s[0] == '\0')
		return;
	skip(s, '\n');
	if(s[0] != COMMAND_PREFIX_CHARACTER) {
		privmsg(channel, s);
		return;
	}
	c = *++s;
	if(c != '\0' && isspac((unsigned char)s[1])) {
		p = s + 2;
		switch(c) {
		case 'j':
			sout("JOIN %s", p);
			if(channel[0] == '\0')
				strlcpy(channel, p, sizeof channel);
			return;
		case 'l':
			s = eat(p, isspac, 1);
			p = eat(s, isspac, 0);
			if(!*s)
				s = channel;
			if(*p)
				*p++ = '\0';
			if(!*p)
				p = DEFAULT_PARTING_MESSAGE;
			sout("PART %s :%s", s, p);
			return;
		case 'm':
			s = eat(p, isspac, 1);
			p = eat(s, isspac, 0);
			if(*p)
				*p++ = '\0';
			privmsg(s, p);
			return;
		case 's':
			strlcpy(channel, p, sizeof channel);
			return;
		}
	}
	sout("%s", s);
}

static void
parsesrv(cmd)
char *cmd;
{
	char *usr, *par, *txt;

	usr = host;
	if(!cmd || !*cmd)
		return;
	if(cmd[0] == ':') {
		usr = cmd + 1;
		cmd = skip(usr, ' ');
		if(cmd[0] == '\0')
			return;
		skip(usr, '!');
	}
	skip(cmd, '\r');
	par = skip(cmd, ' ');
	txt = skip(par, ':');
	trim(par);
	if(!strcmp("PONG", cmd))
		return;
	if(!strcmp("PRIVMSG", cmd))
		pout(par, "<%s> %s", usr, txt);
	else if(!strcmp("PING", cmd))
		sout("PONG %s", txt);
	else {
		pout(usr, ">< %s (%s): %s", cmd, par, txt);
		if(!strcmp("NICK", cmd) && !strcmp(usr, nick))
			strlcpy(nick, txt, sizeof nick);
	}
}


static void
usage() {
	eprint("usage: sic [-h host] [-p port] [-n nick] [-k keyword] [-v]\n", argv0);
}

int
main(argc, argv)
int argc;
char *argv[];
{
	struct timeval tv;
	char *user = getenv("USER");;
	static char vermsg[256];
	int n;
	fd_set rd;

	strlcpy(nick, user ? user : "unknown", sizeof nick);
	ARGBEGIN {
	case 'h':
		host = EARGF(usage());
		break;
	case 'p':
		port = EARGF(usage());
		break;
	case 'n':
		strlcpy(nick, EARGF(usage()), sizeof nick);
		break;
	case 'k':
		password = EARGF(usage());
		break;
	case 'v':
		eprint("sic %s, © 2005-2024 Kris Maglione, Anselm R. Garbe, Nico Golde, Nikola Radojevic\n",
			VERSION);
		break;
	default:
		usage();
	} ARGEND;

	/* init */
	srv = fdopen(dial(host, port), "r+");
	if (!srv)
		eprint("fdopen:");
	/* login */
	if(password)
		sout("PASS %s", password);
	sout("NICK %s", nick);
	sout("USER %s localhost %s :%s", nick, host, nick);
	fflush(srv);
	setbuf(stdout, NULL);
	setbuf(srv, NULL);
	setbuf(stdin, NULL);
#ifdef __OpenBSD__
	if (pledge("stdio", NULL) == -1)
		eprint("error: pledge:");
#endif
	for(;;) { /* main loop */
		FD_ZERO(&rd);
		FD_SET(0, &rd);
		FD_SET(fileno(srv), &rd);
		tv.tv_sec = 120;
		tv.tv_usec = 0;
		n = select(fileno(srv) + 1, &rd, 0, 0, &tv);
		if(n < 0) {
			if(errno == EINTR)
				continue;
			eprint("sic: error on select():");
		}
		else if(n == 0) {
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
