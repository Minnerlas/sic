/* (C)opyright MMV-MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * (C)opyright MMV-MMVI Nico Golde <nico at ngolde dot de>
 * See LICENSE file for license details.
 */
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

#define PINGTIMEOUT 300
#define MAXMSG 4096

enum { Tnick, Tuser, Tcmd, Tchan, Targ, Ttext, Tlast };

static char *server = "irc.oftc.net";
static unsigned short port = 6667;
static char *nick = NULL;
static char *fullname = NULL;
static char *password = NULL;

static char bufin[MAXMSG], bufout[MAXMSG];
static char channel[256];
static int srv;
static time_t trespond;

static int
getline(int fd, unsigned int len, char *buf) {
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
pout(char *channel, char *msg) {
	static char timestr[18];
	time_t t = time(0);

	strftime(timestr, sizeof timestr, "%F %R", localtime(&t));
	fprintf(stdout, "%-8.8s: %s %s\n", channel, timestr, msg);
}

static void
privmsg(char *channel, char *msg) {
	snprintf(bufout, sizeof bufout, "<%s> %s", nick, msg);
	pout(channel, bufout);
	snprintf(bufout, sizeof bufout, "PRIVMSG %s :%s\r\n", channel, msg);
	write(srv, bufout, strlen(bufout));
}

static void
parsein(char *msg) {
	char *p;

	if(msg[0] == 0)
		return;
	if(msg[0] != '/') {
		privmsg(channel, msg);
		return;
	}
	if(!strncmp(msg + 1, "j ", 2) && (msg[3] == '#'))
		snprintf(bufout, sizeof bufout, "JOIN %s\r\n", &msg[3]);
	else if(!strncmp(msg + 1, "l ", 2))
		snprintf(bufout, sizeof bufout, "PART %s :sic\r\n", &msg[3]);
	else if(!strncmp(msg + 1, "m ", 2)) {
		if((p = strchr(&msg[3], ' ')))
			*(p++) = 0;
		privmsg(&msg[3], p);
		return;
	}
	else if(!strncmp(msg + 1, "s ", 2)) {
		strncpy(channel, &msg[3], sizeof channel);
		return;
	}
	else if(!strncmp(msg + 1, "t ", 2)) {
		if((p = strchr(&msg[3], ' ')))
			*(p++) = 0;
		snprintf(bufout, sizeof bufout, "TOPIC %s :%s\r\n", &msg[3], p);
	}
	else
		snprintf(bufout, sizeof bufout, "%s\r\n", &msg[1]);
	write(srv, bufout, strlen(bufout));
}

static unsigned int
tokenize(char **result, unsigned int reslen, char *str, char delim) {
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
parsesrv(char *msg) {
	char *argv[Tlast], *cmd, *p;
	int i;

	if(!msg || !(*msg))
		return;

	for(i = 0; i < Tlast; i++)
		argv[i] = NULL;

	/* <bufout>  ::= [':' <prefix> <SPACE> ] <command> <params> <crlf>
	 * <prefix>   ::= <servername> | <nick> [ '!' <user> ] [ '@' <server> ]
	 * <command>  ::= <letter> { <letter> } | <number> <number> <number>
	 * <SPACE>    ::= ' ' { ' ' }
	 * <params>   ::= <SPACE> [ ':' <trailing> | <middle> <params> ]
	 * <middle>   ::= <Any *non-empty* sequence of octets not including SPACE
	 * or NUL or CR or LF, the first of which may not be ':'>
	 * <trailing> ::= <Any, possibly *empty*, sequence of octets not including NUL or CR or LF>
	 * <crlf>     ::= CR LF
	 */
	if(msg[0] == ':') { /* check prefix */
		if (!(p = strchr(msg, ' ')))
			return;
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
	if(!argv[Tcmd] || !strncmp("PONG", argv[Tcmd], 5))
		return;
	else if(!strncmp("PING", argv[Tcmd], 5)) {
		snprintf(bufout, sizeof bufout, "PONG %s\r\n", argv[Ttext]);
		write(srv, bufout, strlen(bufout));
		return;
	}
	else if(!argv[Tnick] || !argv[Tuser]) {	/* server command */
		snprintf(bufout, sizeof bufout, "%s", argv[Ttext] ? argv[Ttext] : "");
		pout(server, bufout);
		return;
	}
	else if(!strncmp("ERROR", argv[Tcmd], 6))
		snprintf(bufout, sizeof bufout, "-!- error %s",
				argv[Ttext] ? argv[Ttext] : "unknown");
	else if(!strncmp("JOIN", argv[Tcmd], 5)) {
		if(argv[Ttext]!=NULL){
			p = strchr(argv[Ttext], ' ');
		if(p)
			*p = 0;
		}
		argv[Tchan] = argv[Ttext];
		snprintf(bufout, sizeof bufout, "-!- %s(%s) has joined %s",
				argv[Tnick], argv[Tuser], argv[Ttext]);
	}
	else if(!strncmp("PART", argv[Tcmd], 5)) {
		snprintf(bufout, sizeof bufout, "-!- %s(%s) has left %s",
				argv[Tnick], argv[Tuser], argv[Tchan]);
	}
	else if(!strncmp("MODE", argv[Tcmd], 5))
		snprintf(bufout, sizeof bufout, "-!- %s changed mode/%s -> %s %s",
				argv[Tnick], argv[Tcmd + 1] ? argv[Tcmd + 1] : "",
				argv[Tcmd + 2] ? argv[Tcmd + 2] : "",
				argv[Tcmd + 3] ? argv[Tcmd + 3] : "");
	else if(!strncmp("QUIT", argv[Tcmd], 5))
		snprintf(bufout, sizeof bufout, "-!- %s(%s) has quit \"%s\"",
				argv[Tnick], argv[Tuser],
				argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("NICK", argv[Tcmd], 5))
		snprintf(bufout, sizeof bufout, "-!- %s changed nick to %s",
				argv[Tnick], argv[Ttext]);
	else if(!strncmp("TOPIC", argv[Tcmd], 6))
		snprintf(bufout, sizeof bufout, "-!- %s changed topic to \"%s\"",
				argv[Tnick], argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("KICK", argv[Tcmd], 5))
		snprintf(bufout, sizeof bufout, "-!- %s kicked %s (\"%s\")",
				argv[Tnick], argv[Targ],
				argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("NOTICE", argv[Tcmd], 7))
		snprintf(bufout, sizeof bufout, "-!- \"%s\")",
				argv[Ttext] ? argv[Ttext] : "");
	else if(!strncmp("PRIVMSG", argv[Tcmd], 8))
		snprintf(bufout, sizeof bufout, "<%s> %s",
				argv[Tnick], argv[Ttext] ? argv[Ttext] : "");
	if(!argv[Tchan] || !strncmp(argv[Tchan], nick, strlen(nick)))
		pout(argv[Tnick], bufout);
	else
		pout(argv[Tchan], bufout);
}

int
main(int argc, char *argv[]) {
	int i;
	struct timeval tv;
	struct hostent *hp;
	static struct sockaddr_in addr;  /* initially filled with 0's */
	char ping[256];
	fd_set rd;

	nick = fullname = getenv("USER");
	for(i = 1; i < argc; i++)
		if(!strncmp(argv[i], "-s", 3)) {
			if(++i < argc) server = argv[i];
		}
		else if(!strncmp(argv[i], "-p", 3)) {
			if(++i < argc) port = (unsigned short)atoi(argv[i]);
		}
		else if(!strncmp(argv[i], "-n", 3)) {
			if(++i < argc) nick = argv[i];
		}
		else if(!strncmp(argv[i], "-k", 3)) {
			if(++i < argc) password = argv[i];
		}
		else if(!strncmp(argv[i], "-f", 3)) {
			if(++i < argc) fullname = argv[i];
		}
		else if(!strncmp(argv[i], "-v", 3)) {
			fputs("sic-"VERSION", (C)opyright MMVI Anselm R. Garbe\n", stdout);
			exit(EXIT_SUCCESS);
		}
		else {
			fputs("usage: sic [-s server] [-p port] [-n nick]"
					" [-k keyword] [-f fullname] [-v]\n", stderr);
			exit(EXIT_FAILURE);
		}

	/* init */
	if((srv = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "sic: cannot connect server '%s'\n", server);
		exit(EXIT_FAILURE);
	}
	if (NULL == (hp = gethostbyname(server))) {
		fprintf(stderr, "sic: cannot resolve hostname '%s'\n", server);
		exit(EXIT_FAILURE);
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
	if(connect(srv, (struct sockaddr *) &addr, sizeof(struct sockaddr_in))) {
		close(srv);
		fprintf(stderr, "sic: cannot connect server '%s'\n", server);
		exit(EXIT_FAILURE);
	}
	/* login */
	if(password)
		snprintf(bufout, sizeof bufout,
				"PASS %s\r\nNICK %s\r\nUSER %s localhost %s :%s\r\n",
				password, nick, nick, server, fullname);
	else
		snprintf(bufout, sizeof bufout, "NICK %s\r\nUSER %s localhost %s :%s\r\n",
				 nick, nick, server, fullname);
	write(srv, bufout, strlen(bufout));
	snprintf(ping, sizeof ping, "PING %s\r\n", server);
	channel[0] = 0;
	setbuf(stdout, NULL); /* unbuffered stdout */

	for(;;) { /* main loop */
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
				pout(server, "-!- sic shutting down: parse timeout");
				exit(EXIT_FAILURE);
			}
			write(srv, ping, strlen(ping));
			continue;
		}
		if(FD_ISSET(srv, &rd)) {
			if(getline(srv, sizeof bufin, bufin) == -1) {
				perror("sic: remote server closed connection");
				exit(EXIT_FAILURE);
			}
			parsesrv(bufin);
			trespond = time(NULL);
		}
		if(FD_ISSET(0, &rd)) {
			if(getline(0, sizeof bufin, bufin) == -1) {
				perror("sic: broken pipe");
				exit(EXIT_FAILURE);
			}
			parsein(bufin);
		}
	}
	return 0;
}
