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
	if(channel[0] == 0)
		return;
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
	if(msg[0] != ':') {
		privmsg(channel, msg);
		return;
	}
	if(!strncmp(msg + 1, "j ", 2) && (msg[3] == '#'))
		snprintf(bufout, sizeof bufout, "JOIN %s\r\n", &msg[3]);
	else if(!strncmp(msg + 1, "l ", 2))
		snprintf(bufout, sizeof bufout, "PART %s :sic - 250 LOC are too much!\r\n", &msg[3]);
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

static void
parsesrv(char *msg) {
	char *chan, *cmd, *p, *txt, *usr; 

	if(!msg || !(*msg))
		return;
	pout("debug", msg);
	if(msg[0] == ':') { /* check prefix */
		if(!(p = strchr(msg, ' ')))
			return;
		*p = 0;
		usr = &msg[1];
		cmd = ++p;
		if((p = strchr(usr, '!')))
			*p = 0;
	} else
		cmd = msg;
	/* remove CRLFs */
	for(p = cmd; *p; p++)
		if(*p == '\r' || *p == '\n')
			*p = 0;
	if(!strncmp("PONG", cmd, 4))
		return;
	if(!strncmp("PRIVMSG", cmd, 7) || !strncmp("PING", cmd, 4)) {
		if(!(p = strchr(cmd, ' ')))
			return;
		*p = 0;
		chan = ++p;
		for(; *p && *p != ' '; p++);
		*p = 0;
		if(!(p = strchr(++p, ':')))
			return;
		*p = 0;
		txt = ++p;
		if(!strncmp("PRIVMSG", cmd, 8) && chan && txt) {
			snprintf(bufout, sizeof bufout, "<%s> %s", usr, txt);
			pout(chan, bufout);
		}
		else if(!strncmp("PING", cmd, 5) && txt) {
			snprintf(bufout, sizeof bufout, "PONG %s\r\n", txt);
			write(srv, bufout, strlen(bufout));
		}
		return;
	}
	snprintf(bufout, sizeof bufout, "-!- %s", cmd);
	pout(server, bufout);
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
