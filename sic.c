/* See LICENSE file for license details. */
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>

#define PINGTIMEOUT 300
#define MAXMSG      4096
#ifndef VERSION
#define VERSION "dev"
#endif

static void die(const char *errstr, ...);
static void printl(char *channel, char *msg);
static void privmsg(char *channel, char *msg);
static void parsein(char *msg);
static void parsesrv(char *msg);
static int readl(int fd, unsigned int len, char *buf);

static char *host = "irc6.oftc.net";
static char *port = "6667";
static char nick[32];

static char bufin[MAXMSG], bufout[MAXMSG];
static char channel[256];
static int srv;
static time_t trespond;

void
die(const char *errstr, ...) {
	va_list ap;
	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void
printl(char *channel, char *msg) {
	static char timestr[18];
	time_t t = time(0);
	strftime(timestr, sizeof timestr, "%D %R", localtime(&t));
	fprintf(stdout, "%-12.12s: %s %s\n", channel, timestr, msg);
}

void
privmsg(char *channel, char *msg) {
	if(channel[0] == '\0')
		return;
	snprintf(bufout, sizeof bufout, "<%s> %s", nick, msg);
	printl(channel, bufout);
	snprintf(bufout, sizeof bufout, "PRIVMSG %s :%s\r\n", channel, msg);
	write(srv, bufout, strlen(bufout));
}

void
parsein(char *msg) {
	char *p = NULL;
	if(msg[0] == '\0')
		return;
	if(msg[0] != ':') {
		privmsg(channel, msg);
		return;
	}
	if(strncmp(msg + 1, "j ", 2) == 0 && (msg[3] == '#'))
		snprintf(bufout, sizeof bufout, "JOIN %s\r\n", msg + 3);
	else if(strncmp(msg + 1, "l ", 2) == 0)
		snprintf(bufout, sizeof bufout, "PART %s :sic - 250 LOC are too much!\r\n", msg + 3);
	else if(strncmp(msg + 1, "m ", 2) == 0) {
		if((p = strchr(msg + 3, ' ')))
			*(p++) = '\0';
		privmsg(msg + 3, p);
		return;
	}
	else if(strncmp(msg + 1, "s ", 2) == 0) {
		strncpy(channel, msg + 3, sizeof channel - 1);
		return;
	}
	else
		snprintf(bufout, sizeof bufout, "%s\r\n", msg + 1);
	write(srv, bufout, strlen(bufout));
}

void
parsesrv(char *msg) {
	char *chan, *cmd, *p, *txt, *usr; 

	txt = NULL;
	usr = host;
	if(msg == NULL || *msg == '\0' )
		return;
	if(msg[0] != ':')
		cmd = msg;
	else {
		if(!(p = strchr(msg, ' ')))
			return;
		*p = '\0';
		usr = msg + 1;
		cmd = ++p;
		if((p = strchr(usr, '!')))
			*p = '\0';
	}
	for(p = cmd; *p != '\0'; p++) /* remove CRLFs */
		if(*p == '\r' || *p == '\n')
			*p = '\0';
	if((p = strchr(cmd, ':'))) {
		*p = '\0';
		txt = ++p;
	}
	if(strncmp("PONG", cmd, 4) == 0)
		return;
	if(strncmp("PRIVMSG", cmd, 7) == 0 && txt != NULL) {
		if(!(p = strchr(cmd, ' ')))
			return;
		*p = '\0';
		chan = ++p;
		for(; *p != '\0' && *p != ' '; p++);
		*p = '\0';
		snprintf(bufout, sizeof bufout, "<%s> %s", usr, txt);
		printl(chan, bufout);
	}
	else if(strncmp("PING", cmd, 4) == 0 && txt != NULL) {
		snprintf(bufout, sizeof bufout, "PONG %s\r\n", txt);
		write(srv, bufout, strlen(bufout));
	}
	else {
		if (txt != NULL)
			(void) snprintf(bufout, sizeof bufout, ">< %s: %s", cmd, txt);
		else
			(void) snprintf(bufout, sizeof bufout, ">< %s: ", cmd);
		printl(usr, bufout);
		if(strncmp("NICK", cmd, 4) == 0 && strncmp(usr, nick, sizeof nick) == 0 && txt != NULL)
			strncpy(nick, txt, sizeof nick - 1);
	}
}

int
readl(int fd, unsigned int len, char *buf) {
	unsigned int i = 0;
	char c = '\0';

	do {
		if(read(fd, &c, sizeof(char)) != (ssize_t) sizeof(char))
			return -1;
		buf[i++] = c;
	}
	while(c != '\n' && i < len);
	buf[i - 1] = '\0';
	return 0;
}


int
main(const int argc, char *const argv[]) {
	int i;
	struct timeval tv;
	static struct addrinfo hints, *res, *r;
	char ping[256];
	fd_set rd;
	char *password = NULL;

	strncpy(nick, getenv("USER"), sizeof nick - 1);
	for(i = 1; i < argc; i++)
		if(strcmp(argv[i], "-h") == 0) {
			if(++i < argc) host = argv[i];
		}
		else if(strcmp(argv[i], "-p") == 0) {
			if(++i < argc) port = argv[i];
		}
		else if(strcmp(argv[i], "-n") == 0) {
			if(++i < argc) strncpy(nick, argv[i], sizeof nick - 1);
		}
		else if(strcmp(argv[i], "-k") == 0) {
			if(++i < argc) password = argv[i];
		}
		else if(strcmp(argv[i], "-v") == 0)
			die("sic-%s, © 2005-2009 sic engineers\n", VERSION);
		else
			die("usage: sic [-h host] [-p port] [-n nick] [-k keyword] [-v]\n");

	/* init */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if(getaddrinfo(host, port, &hints, &res) != 0)
		die("error: cannot resolve hostname '%s'\n", host);
	for(r = res; r; r = r->ai_next) {
		if((srv = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1)
			continue;
		if(connect(srv, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(srv);
	}
	freeaddrinfo(res);
	if(!r)
		die("error: cannot connect to host '%s'\n", host);

	/* login */
	if (password)
		snprintf(bufout, sizeof bufout,
		        "PASS %s\r\nNICK %s\r\nUSER %s localhost %s :%s\r\n",
		        password, nick, nick, host, nick);
	else
		snprintf(bufout, sizeof bufout, "NICK %s\r\nUSER %s localhost %s :%s\r\n",
		         nick, nick, host, nick);
	write(srv, bufout, strlen(bufout));
	snprintf(ping, sizeof ping, "PING %s\r\n", host);
	channel[0] = '\0';
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
			die("error: error on select()\n");
		}
		else if(i == 0) {
			if(time(NULL) - trespond >= PINGTIMEOUT)
				die("error: sic shutting down: parse timeout\n");
			write(srv, ping, strlen(ping));
			continue;
		}
		if(FD_ISSET(srv, &rd) != 0) {
			if(readl(srv, sizeof bufin, bufin) == -1)
				die("error: remote host closed connection\n");
			parsesrv(bufin);
			trespond = time(NULL);
		}
		if(FD_ISSET(0, &rd) != 0) {
			if(readl(0, sizeof bufin, bufin) == -1)
				die("error: broken pipe\n");
			parsein(bufin);
		}
	}
	return 0;
}
