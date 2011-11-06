/*
 * Small C program to execute arbitrary commands or write to pipes/files
 * upon keypresses of arbitrary scancodes (useful for logitech and other
 * multimedia keyboards.
 * (C)2005, Dan Ponte
 * BSD license
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <signal.h>
#include <X11/Xlib.h>

#define EV_CMD	0x1
#define EV_WRITE	0x2

Display *d;
Window r;
struct keyentry {
	int scancode;
	int type;
	int fd;
	void *data;
	size_t dlen;
	char *command;
	struct keyentry *next;
};

struct keyentry *head = NULL, *last = NULL;
char buf[1024];
int wquit = 0;

int udom_open(fn) /* not really a udom, but oh well...sounds cool */
	char *fn;
{
	int fd;

	fd = open(fn, O_APPEND|O_WRONLY);
	if(fd == -1) {
		perror("udopen");
		exit(-1);
	}

	return fd;
}

void qsig(s)
	int s;
{
	wquit = 1;
}

void get_keys(h)
	struct keyentry *h;
{
	struct keyentry *c;

	for(c = h; c != NULL; c = c->next) {
		XGrabKey(d, c->scancode, AnyModifier, r, False, GrabModeAsync, GrabModeAsync);
	}
}

void init_x(void)
{
	char *dpy;

	dpy = getenv("DISPLAY");
	if(dpy == NULL) {
		fprintf(stderr, "error: needs $DISPLAY\n");
		exit(-1);
	}
	d = XOpenDisplay(dpy);
	if(d == NULL) {
		fprintf(stderr, "error: cannot open display %s\n", dpy);
		exit(-2);
	}
	r = DefaultRootWindow(d);
	if(!r) {
		fprintf(stderr, "error: no root window\n");
		exit(-3);
	}
}


void free_keys(h)
	struct keyentry *h;
{
	struct keyentry *n, *c = h;

	while(c != NULL) {
		n = c->next;
		XUngrabKey(d, c->scancode, AnyModifier, r);
		if(c->command != NULL)
			free(c->command);
		if(c->data != NULL)
			free(c->data);
		if(c->fd != 0)
			close(c->fd);
		free(c);
		c = n;
	}
}

int read_config_file(file, hp, lp)
	char *file;
	struct keyentry **hp;
	struct keyentry **lp;
{
	FILE *f;
	char *kc, *cmd, *r;
	struct keyentry *new;
	int lnum = 0;

	if((f = fopen(file, "r")) == NULL) {
		perror("logikeys");
		exit(-1);
	}

	while(!feof(f)) {
		char *nlt;
		fgets(buf, 1023, f);
		lnum++;
		if(*buf == '#' || *buf == '\n' || *buf == '\0')
			continue;
		if((nlt = strrchr(buf, '\n')) != NULL)
			*nlt = '\0';
	
		kc = buf;
		cmd = buf;
		r = strsep(&cmd, ":");
		if(*r == '\0') {
			fprintf(stderr, "Syntax error at line %d!\n", lnum);
			exit(-1);
		}
		if(cmd == NULL) {
			continue;
		}
		new = calloc(1, sizeof(struct keyentry));
		if(*hp == NULL) {
			*hp = new;
			*lp = new;
		} else {
			(*lp)->next = new;
			*lp = new;
		}
		new->scancode = atoi(kc);
		if(*cmd == '=') {
			struct keyentry *ct;
			int found = 0, tsc;
			char *ssrc;
			char *tpipe = cmd + 1;
			char *tdata = tpipe;

			ssrc = strsep(&tdata, "|");
			if(*ssrc == '\0') {
				fprintf(stderr, "Syntax error on line %d!\n", lnum);
				exit(-1);
			}

			tsc = atoi(tpipe);

			for(ct = *hp; ct != NULL; ct = ct->next) {
				if(ct->scancode == tsc && ct->fd != 0) {
					found = 1;
					break;
				}
			}

			if(!found) {
				fprintf(stderr, "No such scancode defined with pipe %d (line %d)\n", tsc, lnum);
				exit(-1);
			}

			new->type = EV_WRITE;
			new->fd = ct->fd;
			new->data = strdup(tdata);
			new->dlen = strlen(new->data);
		} else if(*cmd == '|') {
			char *pipefn, *dat, *ssret;

			pipefn = cmd + 1;
			dat = pipefn;
			ssret = strsep(&dat, "|");
			if(*ssret == '\0') {
				fprintf(stderr, "Syntax error on line %d!\n", lnum);
				exit(-1);
			}
			if((new->fd = udom_open(pipefn)) == -1) {
				/* not reached? */
				fprintf(stderr, "Error opening pipe %s (line %d)\n", pipefn, lnum);
				exit(-2);
			}
			new->type = EV_WRITE;
			new->data = strdup(dat);
			new->dlen = strlen(new->data);
		} else {
			new->type = EV_CMD;
			new->command = strdup(cmd);
		}
	}

	fclose(f);

	return 1;
}

void doevent(e)
	struct keyentry *e;
{
	char *cmd;
	size_t tl;
	int rc;
	
	switch(e->type) {
		case EV_CMD:
			tl = strlen(e->command) + 4;
			cmd = malloc(tl);
			strlcpy(cmd, e->command, tl);
			strlcat(cmd, " &", tl);
			rc = system(cmd);
			free(cmd);
			return;
		case EV_WRITE:
			if(write(e->fd, e->data, e->dlen) == -1) {
				perror("ev_write");
				wquit = 1;
				return;
			}
			return;
	}
}


void handle_ev(k)
	int k;
{
	struct keyentry *c;

	for(c = head; c != NULL; c = c->next) {
		if(c->scancode == k) {
			doevent(c);
		}
	}
}

void ev_loop(void)
{
	long mask = KeyPressMask;
	XEvent ev;

	while(!wquit) {
		while(XCheckMaskEvent(d, mask, &ev)) {
			handle_ev(ev.xkey.keycode);
		}
		usleep(100000);
	}
}	

void usage(pn)
	char *pn;
{
	printf("%s: usage: %s [-h] [-v] configfile\n", pn, pn);
}

void version(pn)
	char *pn;
{
	printf("%s: logikeys v0.1\n(C)2005, Dan Ponte\n", pn);
}

int main(argc, argv)
	int argc;
	char **argv;
{
	int c;
	char *pn = *argv;

	signal(SIGINT, qsig);
	signal(SIGTERM, qsig);
	while((c = getopt(argc, argv, "hv")) != -1)
		switch(c) {
			case 'h':
				usage(*argv);
				return 1;
			case 'v':
				version(*argv);
				return 1;
		}
	argc -= optind;
	argv += optind;

	if(argc != 1) {
		fprintf(stderr, "%s: must specify config file!\n", pn);
		usage(pn);
		exit(-1);
	}

	read_config_file(*argv, &head, &last);
	
	init_x();
	get_keys(head);
	ev_loop();
	free_keys(head);

	return 0;
}
