/*
 * (C)opyright MMV-MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * (C)opyright MMV-MMVI Nico Golde <nico at ngolde dot de>
 * See LICENSE file for license details.
 */

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#define PINGTIMEOUT 300
#define MAXMSG 4096

enum { Tnick, Tuser, Tcmd, Tchan, Targ, Ttext, Tlast };

/* CUSTOMIZE */
static const char *ping = "PING irc.oftc.net\r\n";
static const char *host = "irc.oftc.net";
static const int port = 6667;
static const char *nick = "arg";
static const char *fullname = "Anselm R. Garbe";
static const char *password = NULL;

static char bufin[MAXMSG], bufout[MAXMSG];
static char channel[256];
static int srv;
static time_t trespond;

static int
getline(int fd, unsigned int len, char *buf)
{
	unsigned int i = 0;
	char c;
	do {
		if(read(fd, &c, sizeof(char)) != sizeof(char))
			return -1;
		buf[i++] = c;
	}
	while(c != '\n' && i < len);
	buf[i - 1] = 0;
	return 0;
}

static void
pout(char *channel, char *msg)
{
	static char timestr[18];
	time_t t = time(0);

	strftime(timestr, sizeof(timestr), "%a %R", localtime(&t));
	fprintf(stdout, "%s: %s %s\n", channel, timestr, msg);
}

static void
privmsg(char *channel, char *msg)
{
	snprintf(bufout, sizeof(bufout), "<%s> %s", nick, msg);
	pout(channel, bufout);
	snprintf(bufout, sizeof(bufout), "PRIVMSG %s :%s\r\n", channel, msg);
	write(srv, bufout, strlen(bufout));
}

static void
parsein(char *msg)
{
	char *p;

	if((p = strchr(msg, ' ')))
		*(p++) = 0;
	if(msg[0] != '/' && msg[0] != 0) {
		privmsg(channel, p);
		return;
	}
	if((p = strchr(&msg[3], ' ')))
		*(p++) = 0;
	switch (msg[1]) {
	case 'j':
		if(msg[3] == '#')
			snprintf(bufout, sizeof(bufout), "JOIN %s\r\n", &msg[3]);
		else if(p) {
			privmsg(&msg[3], p + 1);
			return;
		}
		break;
	case 'l':
		if(p)
			snprintf(bufout, sizeof(bufout), "PART %s :%s\r\n", &msg[3], p);
		else
			snprintf(bufout, sizeof(bufout), "PART %s :sic\r\n", &msg[3]);
		break;
	case 'm':
		privmsg(msg, p);
		break;
	case 's':
		strncpy(channel, msg, sizeof(channel));
		break;
	case 't':
		snprintf(bufout, sizeof(bufout), "TOPIC %s :%s\r\n", &msg[3], p);
		break;
	default:
		snprintf(bufout, sizeof(bufout), "%s\r\n", &msg[1]);
		break;
	}
	write(srv, bufout, strlen(bufout));
}

static unsigned int
tokenize(char **result, unsigned int reslen, char *str, char delim)
{
	char *p, *n;
	unsigned int i = 0;

	if(!str)
		return 0;
	for(n = str; *n == delim; n++);
	p = n;
	for(i = 0; *n != 0;) {
		if(i == reslen)
			return i;
		if(*n == delim) {
			*n = 0;
			if(strlen(p))
				result[i++] = p;
			p = ++n;
		} else
			n++;
	}
	if((i < reslen) && (p < n) && strlen(p))
		result[i++] = p;
	return i;	/* number of tokens */
}

static void
parsesrv(char *msg)
{
	char *argv[Tlast], *cmd, *p;
	int i;
	if(!msg || !(*msg))
		return;

	for(i = 0; i < Tlast; i++)
		argv[i] = NULL;

	/*
	   <bufout>  ::= [':' <prefix> <SPACE> ] <command> <params> <crlf>
	   <prefix>   ::= <servername> | <nick> [ '!' <user> ] [ '@' <host> ]
	   <command>  ::= <letter> { <letter> } | <number> <number> <number>
	   <SPACE>    ::= ' ' { ' ' }
	   <params>   ::= <SPACE> [ ':' <trailing> | <middle> <params> ]
	   <middle>   ::= <Any *non-empty* sequence of octets not including SPACE
	   or NUL or CR or LF, the first of which may not be ':'>
	   <trailing> ::= <Any, possibly *empty*, sequence of octets not including NUL or CR or LF>
	   <crlf>     ::= CR LF
	 */
	if(msg[0] == ':') { /* check prefix */
		p = strchr(msg, ' ');
		*p = 0;
		for(++p; *p == ' '; p++);
		cmd = p;
		argv[Tnick] = &msg[1];
		if((p = strchr(msg, '!'))) {
			*p = 0;
			argv[Tuser] = ++p;
		}
	} else
		cmd = msg;

	/* remove CRLFs */
	for(p = cmd; p && *p != 0; p++)
		if(*p == '\r' || *p == '\n')
			*p = 0;

	if((p = strchr(cmd, ':'))) {
		*p = 0;
		argv[Ttext] = ++p;
	}
	tokenize(&argv[Tcmd], Tlast - Tcmd, cmd, ' ');

	if(!strncmp("PONG", argv[Tcmd], 5)) {
		return;
	} else if(!strncmp("PING", argv[Tcmd], 5)) {
		snprintf(bufout, sizeof(bufout), "PONG %s\r\n", argv[Ttext]);
		write(srv, bufout, strlen(bufout));
		return;
	} else if(!argv[Tnick] || !argv[Tuser]) {	/* server command */
		snprintf(bufout, sizeof(bufout), "%s", argv[Ttext] ? argv[Ttext] : "");
		pout((char *)host, bufout);
		return;
	} else if(!strncmp("ERROR", argv[Tcmd], 6))
		snprintf(bufout, sizeof(bufout), "-!- error %s",
				argv[Ttext] ? argv[Ttext] : "unknown");
	else if(!strncmp("JOIN", argv[Tcmd], 5)) {
		if(argv[Ttext]!=NULL){
			p = strchr(argv[Ttext], ' ');
		if(p)
			*p = 0;
		}
		argv[Tchan] = argv[Ttext];
		snprintf(bufout, sizeof(bufout), "-!- %s(%s) has joined %s",
				argv[Tnick], argv[Tuser], argv[Ttext]);
	} else if(!strncmp("PART", argv[Tcmd], 5)) {
		snprintf(bufout, sizeof(bufout), "-!- %s(%s) has left %s",
				argv[Tnick], argv[Tuser], argv[Tchan]);
	} else if(!strncmp("MODE", argv[Tcmd], 5))
		snprintf(bufout, sizeof(bufout), "-!- %s changed mode/%s -> %s %s",
				argv[Tnick], argv[Tcmd + 1],
				argv[Tcmd + 2], argv[Tcmd + 3]);
	else if(!strncmp("QUIT", argv[Tcmd], 5))
		snprintf(bufout, sizeof(bufout), "-!- %s(%s) has quit \"%s\"",
				argv[Tnick], argv[Tuser],
				argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("NICK", argv[Tcmd], 5))
		snprintf(bufout, sizeof(bufout), "-!- %s changed nick to %s",
				argv[Tnick], argv[Ttext]);
	else if(!strncmp("TOPIC", argv[Tcmd], 6))
		snprintf(bufout, sizeof(bufout), "-!- %s changed topic to \"%s\"",
				argv[Tnick], argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("KICK", argv[Tcmd], 5))
		snprintf(bufout, sizeof(bufout), "-!- %s kicked %s (\"%s\")",
				argv[Tnick], argv[Targ],
				argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("NOTICE", argv[Tcmd], 7))
		snprintf(bufout, sizeof(bufout), "-!- \"%s\")",
				argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("PRIVMSG", argv[Tcmd], 8)) snprintf(bufout, sizeof(bufout), "<%s> %s",
				argv[Tnick], argv[Ttext] ? argv[Ttext] : "");
	if(!argv[Tchan] || !strncmp(argv[Tchan], nick, strlen(nick)))
		pout(argv[Tnick], bufout);
	else
		pout(argv[Tchan], bufout);
}

int
main(int argc, char *argv[])
{
	int i;
	struct timeval tv;
	struct hostent *hp;
	struct sockaddr_in addr = { 0 };
	fd_set rd;

	for(i = 1; (i < argc) && (argv[i][0] == '-'); i++) {
		switch (argv[i][1]) {
		default:
			fputs("usage: sic [-v]\n", stderr);
			exit(EXIT_FAILURE);
			break;
		case 'v':
			fputs("sic-"VERSION", (C)opyright MMVI Anselm R. Garbe\n", stdout);
			exit(EXIT_SUCCESS);
			break;
		}
	}

	/* init */
	if((srv = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "sic: cannot connect server '%s'\n", host);
		exit(EXIT_FAILURE);
	}
	hp = gethostbyname(host);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);
	if(connect(srv, (struct sockaddr *) &addr, sizeof(struct sockaddr_in))) {
		close(srv);
		fprintf(stderr, "sic: cannot connect server '%s'\n", host);
		exit(EXIT_FAILURE);
	}

	/* login */
	if(password)
		snprintf(bufout, sizeof(bufout),
				"PASS %s\r\nNICK %s\r\nUSER %s localhost %s :%s\r\n",
				password, nick, nick, host, fullname ? fullname : nick);
	else
		snprintf(bufout, sizeof(bufout), "NICK %s\r\nUSER %s localhost %s :%s\r\n",
				 nick, nick, host, fullname ? fullname : nick);
	write(srv, bufout, strlen(bufout));

	channel[0] = 0;
	setbuf(stdout, NULL); /* unbuffered stdout */
	for(;;) {
		FD_ZERO(&rd);
		FD_SET(0, &rd);
		FD_SET(srv, &rd);
		tv.tv_sec = 120;
		tv.tv_usec = 0;
		i = select(srv + 1, &rd, 0, 0, &tv);
		if(i < 0) {
			if(errno == EINTR)
				continue;
			perror("sic: error on select()");
			exit(EXIT_FAILURE);
		} else if(i == 0) {
			if(time(NULL) - trespond >= PINGTIMEOUT) {
				pout((char *)host, "-!- sic shutting down: parseing timeout");
				exit(EXIT_FAILURE);
			}
			write(srv, ping, strlen(ping));
			continue;
		}
		if(FD_ISSET(srv, &rd)) {
			if(getline(srv, sizeof(bufin), bufin) == -1) {
				perror("sic: remote host closed connection");
				exit(EXIT_FAILURE);
			}
			parsesrv(bufin);
			trespond = time(NULL);
		}
		if(FD_ISSET(0, &rd)) {
			if(getline(0, sizeof(bufin), bufin) == -1) {
				perror("sic: broken pipe");
				exit(EXIT_FAILURE);
			}
			parsein(bufin);
		}
	}

	return 0;
}
