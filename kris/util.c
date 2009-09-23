#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define va_buf(buf, fmt) {\
	va_list ap; \
	va_start(ap, fmt); \
	vsnprintf(buf, sizeof buf, fmt, ap); \
	va_end(ap); \
}

static void
eprint(const char *fmt, ...) {

	va_buf(bufout, fmt);
	fprintf(stderr, "%s", bufout);

	if(fmt[0] && fmt[strlen(fmt)-1] == ':')
		fprintf(stderr, " %s\n", strerror(errno));
	exit(1);
}

static int
dial(char *host, char *port) {
	static struct addrinfo hints;
	struct addrinfo *res, *r;
	int srv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if(getaddrinfo(host, port, &hints, &res) != 0)
		eprint("error: cannot resolve hostname '%s':", host);
	for(r = res; r; r = r->ai_next) {
		if((srv = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1)
			continue;
		if(connect(srv, r->ai_addr, r->ai_addrlen) == 0)
			break;
		close(srv);
	}
	freeaddrinfo(res);
	if(!r)
		eprint("error: cannot connect to host '%s'\n", host);
	return srv;
}

#define strlcpy _strlcpy
static void
strlcpy(char *to, const char *from, int l) {
	memccpy(to, from, '\0', l);
	to[l-1] = '\0';
}

static void
eat(char **s, int (*p)(int), int r) {
	char *q;

	for(q=*s; *q && p(*q) == r; q++)
		;
	*s = q;
}

static char*
tok(char **s) {
	char *p;

	eat(s, isspace, 1);
	p = *s;
	eat(s, isspace, 0);
	if(**s) *(*s)++ = '\0';
	return p;
}

static char*
ctok(char **s, int c) {
	char *p, *q;

	q = *s;
	for(p=q; *p && *p != c; p++)
		;
	if(*p) *p++ = '\0';
	*s = p;
	return q;
}


