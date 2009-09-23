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
dial(char *host, int port) {
	struct hostent *hp;
	static struct sockaddr_in addr;
	int i;

	if((i = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		eprint("sic: cannot connect host '%s':", host);
	if(nil == (hp = gethostbyname(host)))
		eprint("sic: cannot resolve hostname '%s': %s\n", host, hstrerror(h_errno));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
	if(connect(i, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)))
		eprint("sic: cannot connect host '%s':", host);
	return i;
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


