/*
 * (C)opyright MMV-MMVI Anselm R. Garbe <garbeam at gmail dot com>
 * (C)opyright MMV-MMVI Nico Golde <nico at ngolde dot de>
 * See LICENSE file for license details.
 */

#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

#define PING_TIMEOUT 300
#define MAXMSG 4096

enum { TOK_NICKSRV = 0, TOK_USER, TOK_CMD, TOK_CHAN, TOK_ARG, TOK_TEXT, TOK_LAST };

static int irc;
static time_t last_response;
static char nick[32];			/* might change while running */
static char message[MAXMSG]; /* message buf used for communication */
static char *host = NULL;

static int
tcpopen(char *address)
{
	int fd = 0;
	char *port;
	struct sockaddr_in addr = { 0 };
	struct hostent *hp;
	unsigned int prt;
	
	if((host = strchr(address, '!')))
		*(host++) = 0;

	if(!(port = strrchr(host, '!')))
		return -1;
	*port = 0;
	port++;
	if(sscanf(port, "%d", &prt) != 1)
		return -1;

	/* init */
	if((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1;
	hp = gethostbyname(host);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(prt);
	bcopy(hp->h_addr, &addr.sin_addr, hp->h_length);

	if(connect(fd, (struct sockaddr *) &addr,
				sizeof(struct sockaddr_in))) {
		close(fd);
		return -1;
	}
	return fd;
}

unsigned int
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
print_out(char *channel, char *buf)
{
	static char buft[18];
	time_t t = time(0);

	strftime(buft, sizeof(buft), "%F %R", localtime(&t));
	fprintf(stdout, "%s: %s %s\n", channel, buft, buf);
}

static void
proc_channels_privmsg(char *channel, char *buf)
{
	snprintf(message, MAXMSG, "<%s> %s", nick, buf);
	print_out(channel, message);
	snprintf(message, MAXMSG, "PRIVMSG %s :%s\r\n", channel, buf);
	write(irc, message, strlen(message));
}

static void
proc_channels_input(char *buf)
{
	char *p;

	if((p = strchr(buf, ' ')))
		*(p++) = 0;
	if(buf[0] != '/' && buf[0] != 0) {
		proc_channels_privmsg(buf, p);
		return;
	}
	if((p = strchr(&buf[3], ' ')))
		*(p++) = 0;
	switch (buf[1]) {
	case 'j':
		if(buf[3] == '#')
			snprintf(message, MAXMSG, "JOIN %s\r\n", &buf[3]);
		else if(p) {
			proc_channels_privmsg(&buf[3], p + 1);
			return;
		}
		break;
	case 't':
		snprintf(message, MAXMSG, "TOPIC %s :%s\r\n", &buf[3], p);
		break;
	case 'l':
		if(p)
			snprintf(message, MAXMSG, "PART %s :%s\r\n", &buf[3], p);
		else
			snprintf(message, MAXMSG, "PART %s :sic - 300 SLOC are too much\r\n", &buf[3]);
		write(irc, message, strlen(message));
		return;
		break;
	default:
		snprintf(message, MAXMSG, "%s\r\n", &buf[1]);
		break;
	}
	write(irc, message, strlen(message));
}

static void
proc_server_cmd(char *buf)
{
	char *argv[TOK_LAST], *cmd, *p;
	int i;
	if(!buf || *buf=='\0')
		return;

	for(i = 0; i < TOK_LAST; i++)
		argv[i] = NULL;

	/*
	   <message>  ::= [':' <prefix> <SPACE> ] <command> <params> <crlf>
	   <prefix>   ::= <servername> | <nick> [ '!' <user> ] [ '@' <host> ]
	   <command>  ::= <letter> { <letter> } | <number> <number> <number>
	   <SPACE>    ::= ' ' { ' ' }
	   <params>   ::= <SPACE> [ ':' <trailing> | <middle> <params> ]
	   <middle>   ::= <Any *non-empty* sequence of octets not including SPACE
	   or NUL or CR or LF, the first of which may not be ':'>
	   <trailing> ::= <Any, possibly *empty*, sequence of octets not including NUL or CR or LF>
	   <crlf>     ::= CR LF
	 */
	if(buf[0] == ':') { /* check prefix */
		p = strchr(buf, ' ');
		*p = 0;
		for(++p; *p == ' '; p++);
		cmd = p;
		argv[TOK_NICKSRV] = &buf[1];
		if((p = strchr(buf, '!'))) {
			*p = 0;
			argv[TOK_USER] = ++p;
		}
	} else
		cmd = buf;

	/* remove CRLFs */
	for(p = cmd; p && *p != 0; p++)
		if(*p == '\r' || *p == '\n')
			*p = 0;

	if((p = strchr(cmd, ':'))) {
		*p = 0;
		argv[TOK_TEXT] = ++p;
	}
	tokenize(&argv[TOK_CMD], TOK_LAST - TOK_CMD, cmd, ' ');

	if(!strncmp("PONG", argv[TOK_CMD], 5)) {
		return;
	} else if(!strncmp("PING", argv[TOK_CMD], 5)) {
		snprintf(message, MAXMSG, "PONG %s\r\n", argv[TOK_TEXT]);
		write(irc, message, strlen(message));
		return;
	} else if(!argv[TOK_NICKSRV] || !argv[TOK_USER]) {	/* server command */
		snprintf(message, MAXMSG, "%s", argv[TOK_TEXT] ? argv[TOK_TEXT] : "");
		print_out(0, message);
		return;
	} else if(!strncmp("ERROR", argv[TOK_CMD], 6))
		snprintf(message, MAXMSG, "-!- error %s",
				argv[TOK_TEXT] ? argv[TOK_TEXT] : "unknown");
	else if(!strncmp("JOIN", argv[TOK_CMD], 5)) {
		if(argv[TOK_TEXT]!=NULL){
			p = strchr(argv[TOK_TEXT], ' ');
		if(p)
			*p = 0;
		}
		argv[TOK_CHAN] = argv[TOK_TEXT];
		snprintf(message, MAXMSG, "-!- %s(%s) has joined %s",
				argv[TOK_NICKSRV], argv[TOK_USER], argv[TOK_TEXT]);
	} else if(!strncmp("PART", argv[TOK_CMD], 5)) {
		snprintf(message, MAXMSG, "-!- %s(%s) has left %s",
				argv[TOK_NICKSRV], argv[TOK_USER], argv[TOK_CHAN]);
	} else if(!strncmp("MODE", argv[TOK_CMD], 5))
		snprintf(message, MAXMSG, "-!- %s changed mode/%s -> %s %s",
				argv[TOK_NICKSRV], argv[TOK_CMD + 1],
				argv[TOK_CMD + 2], argv[TOK_CMD + 3]);
	else if(!strncmp("QUIT", argv[TOK_CMD], 5))
		snprintf(message, MAXMSG, "-!- %s(%s) has quit \"%s\"",
				argv[TOK_NICKSRV], argv[TOK_USER],
				argv[TOK_TEXT] ? argv[TOK_TEXT] : "");
	else if(!strncmp("NICK", argv[TOK_CMD], 5))
		snprintf(message, MAXMSG, "-!- %s changed nick to %s",
				argv[TOK_NICKSRV], argv[TOK_TEXT]);
	else if(!strncmp("TOPIC", argv[TOK_CMD], 6))
		snprintf(message, MAXMSG, "-!- %s changed topic to \"%s\"",
				argv[TOK_NICKSRV], argv[TOK_TEXT] ? argv[TOK_TEXT] : "");
	else if(!strncmp("KICK", argv[TOK_CMD], 5))
		snprintf(message, MAXMSG, "-!- %s kicked %s (\"%s\")",
				argv[TOK_NICKSRV], argv[TOK_ARG],
				argv[TOK_TEXT] ? argv[TOK_TEXT] : "");
	else if(!strncmp("NOTICE", argv[TOK_CMD], 7))
		snprintf(message, MAXMSG, "-!- \"%s\")",
				argv[TOK_TEXT] ? argv[TOK_TEXT] : "");
	else if(!strncmp("PRIVMSG", argv[TOK_CMD], 8))
		snprintf(message, MAXMSG, "<%s> %s",
				argv[TOK_NICKSRV], argv[TOK_TEXT] ? argv[TOK_TEXT] : "");
	if(!argv[TOK_CHAN] || !strncmp(argv[TOK_CHAN], nick, strlen(nick)))
		print_out(argv[TOK_NICKSRV], message);
	else
		print_out(argv[TOK_CHAN], message);
}

static int
read_line(int fd, unsigned int res_len, char *buf)
{
	unsigned int i = 0;
	char c;
	do {
		if(read(fd, &c, sizeof(char)) != sizeof(char))
			return -1;
		buf[i++] = c;
	}
	while(c != '\n' && i < res_len);
	buf[i - 1] = 0;			/* eliminates '\n' */
	return 0;
}

static void
handle_server_output()
{
	static char buf[MAXMSG];
	if(read_line(irc, MAXMSG, buf) == -1) {
		perror("sic: remote host closed connection");
		exit(EXIT_FAILURE);
	}
	proc_server_cmd(buf);
}

int
main(int argc, char *argv[])
{
	char address[256];
	char *password = NULL;
	char *fullname = NULL;
	char ping_msg[512], buf[MAXMSG];
	int i, n, r, maxfd;
	struct passwd *spw = getpwuid(getuid());
	struct timeval tv;
	fd_set rd;

	if(!spw) {
		fprintf(stderr,"sic: getpwuid() failed\n"); 
		exit(EXIT_FAILURE);
	}
	snprintf(nick, sizeof(nick), "%s", spw->pw_name);

	if(argc == 2 && argv[1][0] == '-' && argv[1][1] == 'h')
		goto Usage;

	address[0] = 0;
	for(i = 1; (i + 1 < argc) && (argv[i][0] == '-'); i++) {
		switch (argv[i][1]) {
		default:
Usage:
			fputs("usage: sic -a address [-n nick] [-f fullname] [-p password] [-v]\n",
					stdout);
			exit(EXIT_FAILURE);
			break;
		case 'a':
			strncpy(address, argv[++i], sizeof(address));
			break;
		case 'n':
			snprintf(nick, sizeof(nick), "%s", argv[++i]);
			break;
		case 'p':
			password = argv[++i];
			break;
		case 'f':
			fullname = argv[++i];
			break;
		}
	}

	if(!address[0])
		goto Usage;

	if((irc = tcpopen(address)) == -1) {
		fprintf(stderr, "sic: cannot connect server '%s'\n", address);
		exit(EXIT_FAILURE);
	}
	/* login */
	if(password)
		snprintf(message, MAXMSG,
				"PASS %s\r\nNICK %s\r\nUSER %s localhost %s :%s\r\n",
				password, nick, nick, host, fullname ? fullname : nick);
	else
		snprintf(message, MAXMSG, "NICK %s\r\nUSER %s localhost %s :%s\r\n",
				 nick, nick, host, fullname ? fullname : nick);
	write(irc, message, strlen(message));

	snprintf(ping_msg, sizeof(ping_msg), "PING %s\r\n", host);
	for(;;) {
		FD_ZERO(&rd);
		maxfd = irc;
		FD_SET(0, &rd);
		FD_SET(irc, &rd);
		tv.tv_sec = 120;
		tv.tv_usec = 0;
		r = select(maxfd + 1, &rd, 0, 0, &tv);
		if(r < 0) {
			if(errno == EINTR)
				continue;
			perror("sic: error on select()");
			exit(EXIT_FAILURE);
		} else if(r == 0) {
			if(time(NULL) - last_response >= PING_TIMEOUT) {
				print_out(NULL, "-!- sic shutting down: ping timeout");
				exit(EXIT_FAILURE);
			}
			write(irc, ping_msg, strlen(ping_msg));
			continue;
		}
		if(FD_ISSET(irc, &rd)) {
			handle_server_output();
			last_response = time(NULL);
		}
		if(FD_ISSET(0, &rd)) {
			i = n = 0;
			for(;;) {
				if((i = getchar()) == EOF) {
					perror("sic: broken pipe");
					exit(EXIT_FAILURE);
				}
				if(i == '\n' || n >= sizeof(buf) - 1)
					break;
				buf[n++] = i;
			}
			buf[n] = 0;
			proc_channels_input(buf);
		}
	}

	return 0;
}
