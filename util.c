/* See LICENSE file for license details. */
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

extern int errno;

static void
eprint(fmt, va_alist)
char *fmt;
va_dcl
{
	va_list ap;

	va_start(ap);
	vsprintf(bufout, fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s", bufout);
	if(fmt[0] && fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s\n", strerror(errno));
	exit(1);
}

static int
dial(host, port)
char *host;
char *port;
{
	struct sockaddr_in addr;
	int fd, adr, prt = atoi(port);
	struct hostent *hent = gethostbyname(host);

	/*
	struct hostent {
		char *h_name;        official name of host
		char **h_aliases;    alias list
		int  h_addrtype;     host address type
		int  h_length;       length of address
		char **h_addr_list;  list of addresses
	};
	*/

	if (hent->h_addrtype != AF_INET || !hent->h_length) {
		fprintf(stderr, "No IP address\n");
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(prt);
	addr.sin_addr.s_addr = *(unsigned long*)hent->h_addr_list[0];

	if((fd = socket(PF_INET, SOCK_STREAM, 6)) == -1) {
		perror("socket");
		return -1;
	}
	if(connect(fd, &addr, sizeof(addr)) == 0)
		return fd;

	perror("connect");
	return -1;
}

static char *
eat(s, p, r)
char *s;
int (*p)();
int r;
{
	while(*s != '\0' && p((unsigned char)*s) == r)
		s++;
	return s;
}

static char*
skip(s, c)
char *s;
char c;
{
	while(*s != c && *s != '\0')
		s++;
	if(*s != '\0')
		*s++ = '\0';
	return s;
}

static void
trim(s)
char *s;
{
	char *e;

	for (e = s + strlen(s); e > s && isspace((unsigned char)*(e - 1)); e--)
		;
	*e = '\0';
}
