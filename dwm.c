/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */
#include <X11/Xft/Xft.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define ISVISIBLE(C)            (((C)->tags & (C)->mon->tagset[(C)->mon->seltags]) && !(C)->ishidden)
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { PlaceLeft = 1, PlaceRight = 2, PlaceTop = 4,
       PlaceBottom = 8, PlaceMax = 16, PlaceMaxH = 32, PlaceMaxV = 64 }; /* window placement */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetWMWindowTypeDock, NetClientList,
       NetWMHidden, NetWMAbove, NetWMMoveResize, NetWMMaxH, NetWMMaxV, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkStart, ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

enum { SetupLength = 64, SavedSetups = 16 };
typedef struct {
	char setup[SetupLength + 1];
	int monitor, x, y, w, h;
} SavedGeometry;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int floatx, floaty, floatw, floath;
	int fullx, fully, fullw, fullh;
	unsigned int placement, restoreplacement;
	SavedGeometry geometry[SavedSetups];
	unsigned int geometrycount, nextgeometry;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh, hintsvalid;
	int bw, oldbw;
	unsigned int tags;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isdock;
	int ishidden, isabove;
	unsigned int ignoreunmaps;
	unsigned long focused;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win, titlebar;
	Time titleclick;
	int hoverbutton, pressedbutton;
};

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	int showbar;
	int topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
	const Layout *lt[2];
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
} Rule;

/* function declarations */
static void applyrules(Client *c);
static int applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void beginwindowsetup(void);
static void buttonpress(XEvent *e);
static void buttonrelease(XEvent *e);
static void leavenotify(XEvent *e);
static Client *titleclient(Window w);
static int titleh(Client *c);
static int titlebutton(Client *c, int x);
static void createtitle(Client *c);
static void drawtitle(Client *c);
static void dragclient(Client *c, XClientMessageEvent *request);
static void stacktitle(Client *c);
static void inittitlecolors(int bluebird);
static void windowmenu(Client *c);
static void centerclient(const Arg *arg);
static void centerfloating(Client *c);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static int barstartwidth(Monitor *m);
static int barstatuswidth(Monitor *m);
static Client *taskbar(Monitor *m, int clickx);
static void drawbar(Monitor *m);
static void drawbars(void);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Atom getatomprop(Client *c, Atom prop);
static int getrootptr(int *x, int *y);
static long getstate(Window w);
static int gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void minimize(const Arg *arg);
static void hideclient(Client *c);
static void restoreclient(Client *c);
static void updatestate(Client *c);
static void setabove(Client *c, int above);
static void toggleabove(const Arg *arg);
static void keypress(XEvent *e);
static void keyrelease(XEvent *e);
static void alttab(const Arg *arg);
static void drawswitcher(void);
static void endswitcher(int activate);
static void stepswitcher(int direction);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(Monitor *m);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void placeclient(Client *c, unsigned int placement);
static unsigned int maxaxes(Client *c);
static void setmaximized(Client *c, unsigned int axes);
static void pop(Client *c);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static Monitor *recttomon(int x, int y, int w, int h);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restoregeometry(Client *c, const SavedGeometry *g);
static void resizerequest(XEvent *e);
static void selectionclear(XEvent *e);
static void restack(Monitor *m);
static void run(void);
static void savewindowgeometry(void);
static void scan(void);
static int sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void snapclient(const Arg *arg);
static void spawn(const Arg *arg);
static unsigned int statusblockat(int x);
static void statusclick(int x, unsigned int button);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void togglemaximize(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updateclientlist(void);
static int updategeom(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void updatewindowsetup(void);
static void updatetheme(void);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[1024], statusraw[1024];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh;               /* bar height */
static int th;               /* titlebar height */
static Clr *titlecolors[2], *titlegradient[2];
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ButtonRelease] = buttonrelease,
	[LeaveNotify] = leavenotify,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[KeyRelease] = keyrelease,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[MotionNotify] = motionnotify,
	[PropertyNotify] = propertynotify,
	[ResizeRequest] = resizerequest,
	[SelectionClear] = selectionclear,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast], windowsetupatom, statuspidatom, themeatom, wmchangeatom;
static Atom motifatom, csdatom, frameatom;
static char windowsetup[SetupLength + 1];
static int setuppending;
static int running = 1;
static unsigned long focusserial;
static Window focusedclient, movingwin;
static Window switchwin, *switchitems;
static size_t switchcount, switchindex;
static Monitor *switchmon;
static Window switchtarget;
static int switchmenu;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

#include "systray.c"

/* function implementations */
void
applyrules(Client *c)
{
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if ((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
			for (m = mons; m && m->num != r->monitor; m = m->next);
			if (m)
				c->mon = m;
		}
	}
	if (ch.res_class)
		XFree(ch.res_class);
	if (ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	int baseismin, left, top, right, bottom;
	Monitor *m = c->mon, *area;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		left = m->mx; top = m->my;
		right = m->mx + m->mw; bottom = m->my + m->mh;
		for (area = mons; area; area = area->next) {
			left = MIN(left, area->mx);
			top = MIN(top, area->my);
			right = MAX(right, area->mx + area->mw);
			bottom = MAX(bottom, area->my + area->mh);
		}
		if (*x > right)
			*x = right - WIDTH(c);
		if (*y > bottom)
			*y = bottom - HEIGHT(c);
		if (*x + *w + 2 * c->bw < left)
			*x = left;
		if (*y + *h + 2 * c->bw < top)
			*y = top;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}
	if (*h < bh + titleh(c))
		*h = bh + titleh(c);
	if (*w < bh)
		*w = bh;
	if (resizehints || c->isfloating || !c->mon->lt[c->mon->sellt]->arrange) {
		if (!c->hintsvalid)
			updatesizehints(c);
		*h -= titleh(c);
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if (!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if (c->mina > 0 && c->maxa > 0) {
			if (c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if (c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if (baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if (c->incw)
			*w -= *w % c->incw;
		if (c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if (c->maxw)
			*w = MIN(*w, c->maxw);
		if (c->maxh)
			*h = MIN(*h, c->maxh);
		*h += titleh(c);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(Monitor *m)
{
	if (m)
		showhide(m->stack);
	else for (m = mons; m; m = m->next)
		showhide(m->stack);
	if (m) {
		arrangemon(m);
		restack(m);
	} else for (m = mons; m; m = m->next)
		arrangemon(m);
}

void
arrangemon(Monitor *m)
{
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol) - 1);
	m->ltsymbol[sizeof(m->ltsymbol) - 1] = '\0';
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

void
attach(Client *c)
{
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
attachstack(Client *c)
{
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void
beginwindowsetup(void)
{
	if (!setuppending) {
		savewindowgeometry();
		setuppending = 1;
	}
}

void
buttonpress(XEvent *e)
{
	unsigned int i, x, click;
	Arg arg = {0};
	Client *c, *selected = selmon->sel;
	Monitor *m;
	XButtonPressedEvent *ev = &e->xbutton;

	if (switchwin) {
		if (switchmenu) {
			if (ev->button == Button1 && ev->x >= 0
			&& ev->x < MIN(600, switchmon->ww) && ev->y >= 0
			&& ev->y < (int)switchcount * (bh + 8)) {
				switchindex = ev->y / (bh + 8);
				endswitcher(1);
			} else
				endswitcher(0);
		}
		return;
	}
	if ((c = wintoclient(ev->window)) && c->isdock) {
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		return;
	}
	click = ClkRootWin;
	/* focus monitor if necessary */
	m = ev->window == root ? recttomon(ev->x_root, ev->y_root, 1, 1) : wintomon(ev->window);
	if (m && m != selmon) {
		unfocus(selmon->sel, 1);
		selmon = m;
		focus(NULL);
	}
	if (ev->window == selmon->barwin) {
		x = selmon->ww - gettraywidth(selmon) - barstatuswidth(selmon);
		if (ev->x >= 0 && ev->x < barstartwidth(selmon))
			click = ClkStart;
		else if (barstatuswidth(selmon) && ev->x >= (int)x
		&& ev->x < selmon->ww - gettraywidth(selmon)) {
			click = ClkStatusText;
			if (!CLEANMASK(ev->state))
				statusclick(ev->x - x, ev->button);
		} else {
			if (ev->x >= 0 && ev->button == Button1 && !CLEANMASK(ev->state)
			&& (c = taskbar(selmon, ev->x))) {
				if (c == selected && !c->ishidden)
					hideclient(c);
				else {
					restoreclient(c);
					focus(c);
					restack(selmon);
				}
			}
			return;
		}
	} else if ((c = titleclient(ev->window))) {
		focus(c);
		restack(selmon);
		if (CLEANMASK(ev->state)) {
			for (i = 0; i < LENGTH(buttons); i++)
				if (buttons[i].click == ClkClientWin && buttons[i].func
				&& buttons[i].button == ev->button
				&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
					buttons[i].func(&buttons[i].arg);
			return;
		}
		if (ev->button == Button1) {
			c->pressedbutton = titlebutton(c, ev->x);
			if (c->pressedbutton >= 0)
				drawtitle(c);
			else if (c->titleclick && ev->time - c->titleclick <= 300) {
				c->titleclick = 0;
				togglemaximize(NULL);
			} else {
				c->titleclick = ev->time;
				movemouse(NULL);
			}
		} else if (ev->button == Button3)
			windowmenu(c);
		return;
	} else if ((c = wintoclient(ev->window))) {
		focus(c);
		restack(selmon);
		XAllowEvents(dpy, ReplayPointer, CurrentTime);
		click = ClkClientWin;
	}
	for (i = 0; i < LENGTH(buttons); i++)
		if (click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

int
titleh(Client *c)
{
	return c->titlebar && !c->isfullscreen && !c->isdock ? th + 2 * c->bw : 0;
}

Client *
titleclient(Window w)
{
	Monitor *m;
	Client *c;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->titlebar && c->titlebar == w)
				return c;
	return NULL;
}

int
titlebutton(Client *c, int x)
{
	int start = c->w - 3 * th;

	if (!th || start < th || x < start || x >= c->w)
		return -1;
	return (x - start) / th;
}

void
createtitle(Client *c)
{
	Atom actual;
	unsigned char *data = NULL;
	unsigned long n, remaining;
	int format, skip = 0;
	XSetWindowAttributes wa = {0};
	XClassHint hint = {"dwm-titlebar", "Dwm-titlebar"};

	if (!th || c->titlebar || c->isdock
	|| getatomprop(c, netatom[NetWMWindowType]) == netatom[NetWMWindowTypeDock])
		return;
	if (XGetWindowProperty(dpy, c->win, motifatom, 0, 5, False, AnyPropertyType,
		&actual, &format, &n, &remaining, &data) == Success && data) {
		if (format == 32 && n >= 3 && (((long *)data)[0] & 2))
			skip = !(((long *)data)[2] & (1 | 8));
		XFree(data);
	}
	data = NULL;
	if (XGetWindowProperty(dpy, c->win, csdatom, 0, 4, False, XA_CARDINAL,
		&actual, &format, &n, &remaining, &data) == Success && data) {
		if (actual == XA_CARDINAL && format == 32 && n == 4 && !remaining)
			skip = 1;
		XFree(data);
	}
	if (skip)
		return;
	wa.override_redirect = True;
	wa.event_mask = ButtonPressMask|ButtonReleaseMask|ExposureMask
		|PointerMotionMask|EnterWindowMask|LeaveWindowMask;
	c->titlebar = XCreateWindow(dpy, root, c->x, c->y, MAX(c->w, 1), th, c->bw,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect|CWEventMask, &wa);
	c->hoverbutton = c->pressedbutton = -1;
	XSetClassHint(dpy, c->titlebar, &hint);
}

void
stacktitle(Client *c)
{
	XWindowChanges wc = {.sibling = c->win, .stack_mode = Above};

	if (!c->titlebar)
		return;
	if (!titleh(c) || !ISVISIBLE(c))
		XUnmapWindow(dpy, c->titlebar);
	else {
		XMapWindow(dpy, c->titlebar);
		XConfigureWindow(dpy, c->titlebar, CWSibling|CWStackMode, &wc);
	}
}

void
inittitlecolors(int bluebird)
{
	const char *names[3];
	char fg[8], bg[8], border[8], **gradient;
	unsigned int state, row, channel, top, bottom, rgb, value;
	XRenderColor color;

	for (state = 0; state < 2; state++) {
		if (titlecolors[state])
			drw_scm_free(drw, titlecolors[state], 3);
		if (titlegradient[state])
			drw_scm_free(drw, titlegradient[state], th);
		color = scheme[state][ColFg].color;
		snprintf(fg, sizeof fg, "#%02x%02x%02x", color.red >> 8, color.green >> 8, color.blue >> 8);
		color = scheme[state][ColBg].color;
		snprintf(bg, sizeof bg, "#%02x%02x%02x", color.red >> 8, color.green >> 8, color.blue >> 8);
		color = scheme[state][ColBorder].color;
		snprintf(border, sizeof border, "#%02x%02x%02x", color.red >> 8, color.green >> 8, color.blue >> 8);
		names[0] = bluebird ? (state ? "#f1f1f1" : "#93adc9") : fg;
		names[1] = bluebird ? (state ? "#4e7aa9" : "#4e7aa7") : bg;
		names[2] = bluebird ? "#385b80" : border;
		titlecolors[state] = drw_scm_create(drw, names, 3);
		top = bluebird ? (state ? 0x5c88b7 : 0x7294b7) : strtoul(bg + 1, NULL, 16);
		bottom = bluebird ? (state ? 0x4e7aa9 : 0x4e7aa7) : top;
		gradient = ecalloc(th, sizeof(char *));
		for (row = 0; row < (unsigned int)th; row++) {
			rgb = 0;
			for (channel = 0; channel < 3; channel++) {
				value = (((top >> (8 * channel)) & 255) * (th - 1 - row)
					+ ((bottom >> (8 * channel)) & 255) * row) / MAX(1, th - 1);
				rgb |= value << (8 * channel);
			}
			gradient[row] = ecalloc(8, 1);
			snprintf(gradient[row], 8, "#%06x", rgb);
		}
		titlegradient[state] = drw_scm_create(drw, (const char **)gradient, th);
		for (row = 0; row < (unsigned int)th; row++)
			free(gradient[row]);
		free(gradient);
	}
}

void
drawtitle(Client *c)
{
	int state, y, i, buttons, width, padding;
	Clr row[3];
	const char *labels[] = {"−", c->placement & PlaceMax ? "▣" : "□", "×"};

	if (!titleh(c) || !ISVISIBLE(c))
		return;
	state = c == selmon->sel && c->mon == selmon;
	XSetWindowBorder(dpy, c->titlebar, titlecolors[state][ColBorder].pixel);
	drw_resize(drw, MAX(sw, c->w), MAX(bh, th));
	for (y = 0; y < th; y++) {
		row[ColFg] = row[ColBg] = row[ColBorder] = titlegradient[state][y];
		drw_setscheme(drw, row);
		drw_rect(drw, 0, y, c->w, 1, 1, 0);
	}
	buttons = c->w >= 4 * th ? 3 * th : 0;
	width = c->w - buttons;
	padding = MAX(lrpad / 2, (c->w - (int)TEXTW(c->name) + lrpad) / 2);
	padding = MIN(padding, MAX(0, width - lrpad));
	drw_setscheme(drw, titlecolors[state]);
	drw_text(drw, 0, 0, width, th, padding, c->name, 2);
	for (i = 0; buttons && i < 3; i++)
		drw_text(drw, width + i * th, 0, th, th,
			MAX(0, (th - (int)TEXTW(labels[i]) + lrpad) / 2), labels[i],
			c->pressedbutton == i || c->hoverbutton == i ? 1 : 2);
	drw_map(drw, c->titlebar, 0, 0, c->w, th);
}

void
buttonrelease(XEvent *e)
{
	Client *c = titleclient(e->xbutton.window);
	int button;

	if (!c || e->xbutton.button != Button1)
		return;
	button = c->pressedbutton;
	c->pressedbutton = -1;
	if (button < 0 || button != titlebutton(c, e->xbutton.x)
	|| e->xbutton.y < 0 || e->xbutton.y >= th) {
		drawtitle(c);
		return;
	}
	focus(c);
	if (button == 0)
		hideclient(c);
	else if (button == 1)
		togglemaximize(NULL);
	else
		killclient(NULL);
	drawtitle(c);
}

void
leavenotify(XEvent *e)
{
	Client *c = titleclient(e->xcrossing.window);

	if (c) {
		c->hoverbutton = -1;
		drawtitle(c);
	}
}

void
centerclient(const Arg *arg)
{
	Client *c = selmon->sel;
	Monitor *m;

	if (!c || c->isfullscreen)
		return;
	if (c->placement)
		placeclient(c, 0);
	m = c->mon;
	resize(c, m->wx + (m->ww - WIDTH(c)) / 2,
		m->wy + (m->wh - HEIGHT(c)) / 2, c->w, c->h, 0);
}

void
centerfloating(Client *c)
{
	Monitor *m = c->mon;

	if (!c->isfloating || c->isdock || c->isfullscreen)
		return;
	c->x = c->oldx = m->wx + MAX(0, (m->ww - WIDTH(c)) / 2);
	c->y = c->oldy = m->wy + MAX(0, (m->wh - HEIGHT(c)) / 2);
}

void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void)
{
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;
	size_t i;

	if (switchwin)
		endswitcher(0);
	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for (m = mons; m; m = m->next)
		while (m->stack)
			unmanage(m->stack, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	cleanuptray();
	while (mons)
		cleanupmon(mons);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		drw_scm_free(drw, scheme[i], 3);
	free(scheme);
	for (i = 0; i < 2; i++) {
		drw_scm_free(drw, titlecolors[i], 3);
		drw_scm_free(drw, titlegradient[i], th);
	}
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	if (traymon == mon) {
		XReparentWindow(dpy, traywin, root, 0, 0);
		traymon = NULL;
	}
	if (statusmon == mon)
		statusmon = mons;
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);
	unsigned int axes, requested;

	if (traywin && cme->window == traywin && cme->message_type == trayopcode) {
		if (cme->format == 32 && cme->data.l[1] == 0)
			docktrayicon(cme->data.l[2]);
		return;
	}
	if (!c || c->isdock)
		return;
	if (cme->message_type == netatom[NetWMMoveResize]) {
		if (cme->format != 32 || cme->data.l[2] < 0 || cme->data.l[2] > 8
		|| (cme->data.l[3] != 0 && cme->data.l[3] != Button1)
		|| cme->data.l[4] < 0 || cme->data.l[4] > 2
		|| cme->data.l[0] < SHRT_MIN || cme->data.l[0] > SHRT_MAX
		|| cme->data.l[1] < SHRT_MIN || cme->data.l[1] > SHRT_MAX)
			return;
		dragclient(c, cme);
	} else if (cme->message_type == netatom[NetWMState]) {
		if (cme->format != 32 || cme->data.l[0] < 0 || cme->data.l[0] > 2)
			return;
		axes = maxaxes(c);
		requested = 0;
		if (cme->data.l[1] == netatom[NetWMMaxH] || cme->data.l[2] == netatom[NetWMMaxH])
			requested |= PlaceMaxH;
		if (cme->data.l[1] == netatom[NetWMMaxV] || cme->data.l[2] == netatom[NetWMMaxV])
			requested |= PlaceMaxV;
		if (requested)
			setmaximized(c, cme->data.l[0] == 0 ? axes & ~requested
				: cme->data.l[0] == 1 ? axes | requested : axes ^ requested);
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
		if (cme->data.l[1] == netatom[NetWMAbove]
		|| cme->data.l[2] == netatom[NetWMAbove])
			setabove(c, cme->data.l[0] == 1
				|| (cme->data.l[0] == 2 && !c->isabove));
	} else if (cme->message_type == wmchangeatom && cme->format == 32
	&& cme->data.l[0] == IconicState) {
		hideclient(c);
	} else if (cme->message_type == netatom[NetActiveWindow] && cme->format == 32) {
		restoreclient(c);
		focus(c);
		restack(c->mon);
	}
}

void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y + titleh(c);
	ce.width = c->w;
	ce.height = MAX(1, c->h - titleh(c));
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty, pending;

	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		if (switchwin)
			endswitcher(0);
		if (ev->width <= 0 || ev->height <= 0)
			return;

		pending = setuppending;
		beginwindowsetup();
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, bh);
			updatebars();
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					if (c->isfullscreen)
						resizeclient(c, m->mx, m->my, m->mw, m->mh);
				XMoveResizeWindow(dpy, m->barwin, m->wx, m->by, m->ww, bh);
			}
			focus(NULL);
			arrange(NULL);
		} else if (!pending)
			setuppending = 0;
		updateprimary();
		drawbars();
	}
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;
	TrayIcon *i = wintotrayicon(ev->window);

	if (i) {
		trayicongeom(i, ev->value_mask & CWWidth ? ev->width : i->w,
			ev->value_mask & CWHeight ? ev->height : i->h);
		updatesystray();
		drawbars();
		return;
	}
	if ((c = wintoclient(ev->window))) {
		if (c->isdock) {
			if (ev->value_mask & CWX) c->x = ev->x;
			if (ev->value_mask & CWY) c->y = ev->y;
			if (ev->value_mask & CWWidth) c->w = MAX(1, ev->width);
			if (ev->value_mask & CWHeight) c->h = MAX(1, ev->height);
			c->mon = recttomon(c->x, c->y, c->w, c->h);
			XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
			configure(c);
			XSync(dpy, False);
			return;
		}
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = MAX(1, ev->height) + titleh(c);
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if (ISVISIBLE(c))
				resizeclient(c, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void)
{
	Monitor *m;

	m = ecalloc(1, sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof(m->ltsymbol) - 1);
	m->ltsymbol[sizeof(m->ltsymbol) - 1] = '\0';
	return m;
}

void
destroynotify(XEvent *e)
{
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;
	TrayIcon *i = wintotrayicon(ev->window);

	if (i) {
		removetrayicon(i, 1);
		updatesystray();
		drawbars();
		return;
	}
	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c)
{
	Client **tc, *t;

	for (tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if (c == c->mon->sel) {
		for (t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

Monitor *
dirtomon(int dir)
{
	Monitor *m = NULL;

	if (dir > 0) {
		if (!(m = selmon->next))
			m = mons;
	} else if (selmon == mons)
		for (m = mons; m->next; m = m->next);
	else
		for (m = mons; m->next != selmon; m = m->next);
	return m;
}

int
barstartwidth(Monitor *m)
{
	return MIN((int)TEXTW("Inicio"), MAX(0, m->ww - gettraywidth(m)));
}

int
barstatuswidth(Monitor *m)
{
	return m == statusmon ? MIN(MAX(0, m->ww - gettraywidth(m) - barstartwidth(m)),
		MAX(0, (int)TEXTW(stext) - lrpad + 2)) : 0;
}

/* Drawing and clicks share the same boundaries, including rounding. */
Client *
taskbar(Monitor *m, int clickx)
{
	Client *c;
	int n = 0, i = 0, x, end, start = barstartwidth(m);
	int width = MAX(0, m->ww - gettraywidth(m) - barstatuswidth(m) - start);
	char label[sizeof c->name + 8];

	for (c = m->clients; c; c = c->next)
		if (!c->isdock && (c->tags & m->tagset[m->seltags]))
			n++;
	for (c = m->clients; c; c = c->next) {
		if (c->isdock || !(c->tags & m->tagset[m->seltags]))
			continue;
		x = start + (long)i * width / n;
		end = start + (long)++i * width / n;
		if (clickx >= x && clickx < end)
			return c;
		if (clickx >= 0 || end <= x)
			continue;
		snprintf(label, sizeof label, "%s%s", c->ishidden ? "[min] " : "", c->name);
		drw_setscheme(drw, scheme[m == selmon && c == m->sel && !c->ishidden
			? SchemeSel : SchemeNorm]);
		drw_text(drw, x, 0, end - x, bh, MIN(lrpad / 2, end - x), label, c->isurgent);
	}
	return NULL;
}

void
drawbar(Monitor *m)
{
	int start = barstartwidth(m), status = barstatuswidth(m);
	int width = MAX(0, m->ww - gettraywidth(m));

	if (!m->showbar || !width)
		return;
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, width, bh, 1, 1);
	if (start)
		drw_text(drw, 0, 0, start, bh, MIN(lrpad / 2, start), "Inicio", 0);
	if (status)
		drw_text(drw, width - status, 0, status, bh, 0, stext, 0);
	taskbar(m, -1);
	drw_map(drw, m->barwin, 0, 0, m->ww, bh);
}

void
drawbars(void)
{
	Monitor *m;

	Client *c;

	for (m = mons; m; m = m->next) {
		drawbar(m);
		for (c = m->clients; c; c = c->next)
			drawtitle(c);
	}
}

void
expose(XEvent *e)
{
	Monitor *m;
	XExposeEvent *ev = &e->xexpose;

	Client *c;

	if ((c = titleclient(ev->window))) {
		drawtitle(c);
		return;
	}
	if (ev->window == switchwin) {
		drawswitcher();
		return;
	}
	if (ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);
}

void
focus(Client *c)
{
	if (c && (c->isdock || c->ishidden))
		return;
	if (c && c->mon != selmon) {
		unfocus(selmon->sel, 0);
		selmon = c->mon;
	}
	if (!c || !ISVISIBLE(c))
		for (c = selmon->stack; c && (!ISVISIBLE(c) || c->isdock); c = c->snext);
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (focusedclient != c->win || !c->focused)
			c->focused = ++focusserial;
		focusedclient = c->win;
		if (c->isurgent)
			seturgent(c, 0);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, 1);
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		setfocus(c);
	} else {
		focusedclient = None;
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	selmon->sel = c;
	drawbars();
}

/* there are some broken focus acquiring clients needing extra handling */
void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win)
		setfocus(selmon->sel);
}

void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (!mons->next)
		return;
	if ((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(NULL);
}

void
focusstack(const Arg *arg)
{
	Client *c = NULL, *i;

	if (!selmon->sel || (selmon->sel->isfullscreen && lockfullscreen))
		return;
	if (arg->i > 0) {
		for (c = selmon->sel->next; c && (!ISVISIBLE(c) || c->isdock); c = c->next);
		if (!c)
			for (c = selmon->clients; c && (!ISVISIBLE(c) || c->isdock); c = c->next);
	} else {
		for (i = selmon->clients; i != selmon->sel; i = i->next)
			if (ISVISIBLE(i) && !i->isdock)
				c = i;
		if (!c)
			for (; i; i = i->next)
				if (ISVISIBLE(i) && !i->isdock)
					c = i;
	}
	if (c) {
		focus(c);
		restack(selmon);
	}
}

Atom
getatomprop(Client *c, Atom prop)
{
	int format;
	unsigned long nitems, dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &format, &nitems, &dl, &p) == Success && p) {
		if (nitems > 0 && format == 32)
			atom = *(long *)p;
		XFree(p);
	}
	return atom;
}

int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w)
{
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
		&real, &format, &n, &extra, &p) != Success)
		return -1;
	if (n != 0 && format == 32)
		result = *(long *)p;
	XFree(p);
	return result;
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING) {
		strncpy(text, (char *)name.value, size - 1);
	} else if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success) {
		if (n > 0 && *list)
			strncpy(text, *list, size - 1);
		XFreeStringList(list);
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}

void
grabbuttons(Client *c, int focused)
{
	if (c->isdock) {
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		return;
	}
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if (!focused)
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
				BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		else
			for (j = 0; j < LENGTH(modifiers); j++)
				XGrabButton(dpy, Button1, modifiers[j], c->win, False,
					BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
		for (i = 0; i < LENGTH(buttons); i++)
			if (buttons[i].click == ClkClientWin)
				for (j = 0; j < LENGTH(modifiers); j++)
					XGrabButton(dpy, buttons[i].button,
						buttons[i].mask | modifiers[j],
						c->win, False, BUTTONMASK,
						GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void)
{
	updatenumlockmask();
	{
		unsigned int i, j, k;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		int start, end, skip;
		KeySym *syms;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		XDisplayKeycodes(dpy, &start, &end);
		syms = XGetKeyboardMapping(dpy, start, end - start + 1, &skip);
		if (!syms)
			return;
		for (k = start; k <= end; k++)
			for (i = 0; i < LENGTH(keys); i++)
				/* skip modifier codes, we do that ourselves */
				if (keys[i].keysym == syms[(k - start) * skip])
					for (j = 0; j < LENGTH(modifiers); j++)
						XGrabKey(dpy, k,
							 keys[i].mod | modifiers[j],
							 root, True,
							 GrabModeAsync, GrabModeAsync);
		XFree(syms);
	}
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	if (switchwin) {
		if (keysym == XK_Tab || keysym == XK_ISO_Left_Tab)
			stepswitcher(ev->state & ShiftMask ? -1 : 1);
		else if (keysym == XK_Up)
			stepswitcher(-1);
		else if (keysym == XK_Down)
			stepswitcher(1);
		else if (keysym == XK_Escape)
			endswitcher(0);
		else if (keysym == XK_Return || keysym == XK_KP_Enter)
			endswitcher(1);
		return;
	}
	for (i = 0; i < LENGTH(keys); i++)
		if (keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
keyrelease(XEvent *e)
{
	KeySym key;
	KeyCode left, right;
	char held[32];

	if (!switchwin || switchmenu)
		return;
	key = XKeycodeToKeysym(dpy, e->xkey.keycode, 0);
	if (key != XK_Alt_L && key != XK_Alt_R)
		return;
	XQueryKeymap(dpy, held);
	left = XKeysymToKeycode(dpy, XK_Alt_L);
	right = XKeysymToKeycode(dpy, XK_Alt_R);
	if (!(left && (held[left / 8] & (1 << (left % 8))))
	&& !(right && (held[right / 8] & (1 << (right % 8)))))
		endswitcher(1);
}

void
drawswitcher(void)
{
	Client *c;
	size_t i, start, rows;
	int w, rowh;
	char label[320];

	if (!switchwin || !switchcount)
		return;
	w = MIN(600, switchmon->ww);
	rowh = bh + 8;
	rows = MIN(switchcount, MAX(1, switchmon->wh / rowh));
	start = switchindex / rows * rows;
	rows = MIN(rows, switchcount - start);
	XMoveResizeWindow(dpy, switchwin, switchmon->wx + (switchmon->ww - w) / 2,
		switchmon->wy + (switchmon->wh - rows * rowh) / 2, w, rows * rowh);
	drw_resize(drw, MAX(sw, w), MAX(bh, rows * rowh));
	for (i = 0; i < rows; i++) {
		if (switchmenu) {
			const char *actions[] = {"Siempre encima", "Minimizar", "Maximizar/restaurar", "Cerrar"};
			c = wintoclient(switchtarget);
			snprintf(label, sizeof label, "%s%s", start + i == 0 && c && c->isabove ? "✓ " : "",
				actions[start + i]);
		} else {
			c = wintoclient(switchitems[start + i]);
			snprintf(label, sizeof label, "%s%s%s", c && c->ishidden ? "[min] " : "",
				c && c->isabove ? "[top] " : "", c ? c->name : "");
		}
		drw_setscheme(drw, scheme[start + i == switchindex ? SchemeSel : SchemeNorm]);
		drw_text(drw, 0, i * rowh, w, rowh, lrpad / 2, label, 0);
	}
	drw_map(drw, switchwin, 0, 0, w, rows * rowh);
	XRaiseWindow(dpy, switchwin);
}

void
stepswitcher(int direction)
{
	if (!switchcount)
		return;
	if (direction < 0)
		switchindex = switchindex ? switchindex - 1 : switchcount - 1;
	else
		switchindex = (switchindex + 1) % switchcount;
	drawswitcher();
}

void
endswitcher(int activate)
{
	Client *c = NULL;
	int menu = switchmenu, action = switchindex;

	if (activate && switchcount)
		c = wintoclient(menu ? switchtarget : switchitems[switchindex]);
	if (switchwin) {
		if (menu)
			XUngrabPointer(dpy, CurrentTime);
		XUngrabKeyboard(dpy, CurrentTime);
		XDestroyWindow(dpy, switchwin);
	}
	switchwin = None;
	switchmenu = 0;
	switchtarget = None;
	free(switchitems);
	switchitems = NULL;
	switchcount = switchindex = 0;
	if (c) {
		restoreclient(c);
		focus(c);
		restack(c->mon);
		if (menu) {
			if (action == 0)
				setabove(c, !c->isabove);
			else if (action == 1)
				hideclient(c);
			else if (action == 2)
				togglemaximize(NULL);
			else
				killclient(NULL);
		}
	}
	/* Discard crossings generated by removing the popup, not real motion. */
	{
		XEvent event;
		while (XCheckMaskEvent(dpy, EnterWindowMask, &event));
	}
}

void
windowmenu(Client *c)
{
	XSetWindowAttributes wa = {0};

	if (!c || c->isdock || switchwin)
		return;
	if (XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
		CurrentTime) != GrabSuccess)
		return;
	switchmenu = 1;
	switchtarget = c->win;
	switchmon = c->mon;
	switchcount = 4;
	switchindex = 0;
	wa.override_redirect = True;
	wa.event_mask = ExposureMask|ButtonPressMask;
	switchwin = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, CopyFromParent,
		InputOutput, CopyFromParent, CWOverrideRedirect|CWEventMask, &wa);
	XMapRaised(dpy, switchwin);
	if (XGrabPointer(dpy, switchwin, False, ButtonPressMask|ButtonReleaseMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
		endswitcher(0);
		return;
	}
	drawswitcher();
}

void
alttab(const Arg *arg)
{
	Monitor *m;
	Client *c, *previous;
	size_t n = 0, i, j;
	XSetWindowAttributes wa = {0};

	if (switchwin) {
		stepswitcher(arg->i);
		return;
	}
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (!c->isdock && (c->tags & m->tagset[m->seltags]))
				n++;
	if (!n)
		return;
	if (XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync,
		CurrentTime) != GrabSuccess)
		return;
	switchitems = ecalloc(n, sizeof(Window));
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next) {
			if (c->isdock || !(c->tags & m->tagset[m->seltags]))
				continue;
			for (i = 0; i < switchcount; i++) {
				previous = wintoclient(switchitems[i]);
				if (c == selmon->sel || (previous != selmon->sel
				&& c->focused > previous->focused))
					break;
			}
			for (j = switchcount; j > i; j--)
				switchitems[j] = switchitems[j - 1];
			switchitems[i] = c->win;
			switchcount++;
		}
	switchmon = selmon;
	wa.override_redirect = True;
	wa.event_mask = ExposureMask;
	switchwin = XCreateWindow(dpy, root, 0, 0, 1, 1, 0, CopyFromParent,
		InputOutput, CopyFromParent, CWOverrideRedirect|CWEventMask, &wa);
	XMapRaised(dpy, switchwin);
	switchindex = 0;
	stepswitcher(arg->i);
}

/* Preserve states owned by applications when updating our EWMH flags. */
void
updatestate(Client *c)
{
	Atom actual = None, *states;
	unsigned char *data = NULL;
	unsigned long n = 0, extra, i, count = 0;
	int format = 0;

	if (XGetWindowProperty(dpy, c->win, netatom[NetWMState], 0, 1024,
		False, XA_ATOM, &actual, &format, &n, &extra, &data) != Success
	|| actual != XA_ATOM || format != 32 || extra)
		n = 0;
	states = ecalloc(n + 5, sizeof(Atom));
	for (i = 0; i < n; i++)
		if (((Atom *)data)[i] != netatom[NetWMFullscreen]
		&& ((Atom *)data)[i] != netatom[NetWMHidden]
		&& ((Atom *)data)[i] != netatom[NetWMAbove]
		&& ((Atom *)data)[i] != netatom[NetWMMaxH]
		&& ((Atom *)data)[i] != netatom[NetWMMaxV])
			states[count++] = ((Atom *)data)[i];
	if (c->isfullscreen)
		states[count++] = netatom[NetWMFullscreen];
	if (c->ishidden)
		states[count++] = netatom[NetWMHidden];
	if (c->isabove)
		states[count++] = netatom[NetWMAbove];
	if (maxaxes(c) & PlaceMaxH)
		states[count++] = netatom[NetWMMaxH];
	if (maxaxes(c) & PlaceMaxV)
		states[count++] = netatom[NetWMMaxV];
	XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		PropModeReplace, (unsigned char *)states, count);
	if (data)
		XFree(data);
	free(states);
}

void
setabove(Client *c, int above)
{
	if (!c || c->isdock)
		return;
	c->isabove = !!above;
	updatestate(c);
	restack(c->mon);
}

void
toggleabove(const Arg *arg)
{
	if (selmon->sel)
		setabove(selmon->sel, !selmon->sel->isabove);
}

void
hideclient(Client *c)
{
	XWindowAttributes wa;

	if (!c || c->isdock || c->ishidden)
		return;
	c->ishidden = 1;
	if (XGetWindowAttributes(dpy, c->win, &wa) && wa.map_state != IsUnmapped) {
		/* We select StructureNotify on the client and SubstructureNotify on root. */
		c->ignoreunmaps += 2;
		XUnmapWindow(dpy, c->win);
	}
	setclientstate(c, IconicState);
	updatestate(c);
	if (c == selmon->sel)
		focus(NULL);
	else if (c == c->mon->sel)
		c->mon->sel = NULL;
	arrange(c->mon);
}

void
minimize(const Arg *arg)
{
	hideclient(selmon->sel);
}

void
restoreclient(Client *c)
{
	if (!c || !c->ishidden)
		return;
	c->ishidden = 0;
	XMapWindow(dpy, c->win);
	setclientstate(c, NormalState);
	updatestate(c);
	arrange(c->mon);
}

void
killclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	sendevent(selmon->sel, wmatom[WMDelete]);
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;

	c = ecalloc(1, sizeof(Client));
	c->win = w;
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
		c->mon = t->mon;
		c->tags = t->tags;
	} else {
		c->mon = selmon;
		applyrules(c);
	}

	c->bw = borderpx;
	createtitle(c);
	c->h = c->oldh = c->h + titleh(c);
	updatewindowtype(c);
	if (c->isdock)
		c->mon = recttomon(c->x, c->y, c->w, c->h);
	if (c->x + WIDTH(c) > c->mon->wx + c->mon->ww)
		c->x = c->mon->wx + c->mon->ww - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->wy + c->mon->wh)
		c->y = c->mon->wy + c->mon->wh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->wx);
	c->y = MAX(c->y, c->mon->wy);

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, 0);
	if (!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	centerfloating(c);
	updatewindowtype(c);
	configure(c); /* propagates border_width and initial position */
	if (c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y + titleh(c),
		c->w, MAX(1, c->h - titleh(c))); /* some windows require this */
	setclientstate(c, NormalState);
	if (!c->isdock) {
		if (c->mon == selmon)
			unfocus(selmon->sel, 0);
		c->mon->sel = c;
	}
	arrange(c->mon);
	XMapWindow(dpy, c->win);
	focus(NULL);
}

void
mappingnotify(XEvent *e)
{
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if (ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;
	TrayIcon *i = wintotrayicon(ev->window);

	if (i) {
		trayiconstate(i);
		updatesystray();
		drawbars();
		return;
	}
	if (!XGetWindowAttributes(dpy, ev->window, &wa) || wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
	else
		restoreclient(wintoclient(ev->window));
}

void
monocle(Monitor *m)
{
	unsigned int n = 0;
	Client *c;

	for (c = m->clients; c; c = c->next)
		if (ISVISIBLE(c))
			n++;
	if (n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for (c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, 0);
}

void
motionnotify(XEvent *e)
{
	XMotionEvent *ev = &e->xmotion;
	Client *c = titleclient(ev->window);

	if (c) {
		c->hoverbutton = titlebutton(c, ev->x);
		drawtitle(c);
		return;
	}
}

void
dragclient(Client *c, XClientMessageEvent *request)
{
	static const unsigned int edges[] = {
		PlaceTop | PlaceLeft, PlaceTop, PlaceTop | PlaceRight, PlaceRight,
		PlaceBottom | PlaceRight, PlaceBottom, PlaceBottom | PlaceLeft, PlaceLeft
	};
	int x, y, px, py, ocx, ocy, ocw, och, nx, ny, nw, nh, offsety;
	unsigned int mask, sides = request && request->data.l[2] < 8 ? edges[request->data.l[2]] : 0;
	double anchor;
	Window dummy, win;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (movingwin || !c || c->isdock || c->isfullscreen || !ISVISIBLE(c))
		return;
	if (sides) {
		if (!c->hintsvalid)
			updatesizehints(c);
		if (c->isfixed)
			return;
	}
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[sides ? CurResize : CurMove]->cursor, CurrentTime) != GrabSuccess)
		return;
	/* A release can precede the client's request; never wait for a lost release. */
	if (!XQueryPointer(dpy, root, &dummy, &dummy, &x, &y, &px, &py, &mask)
	|| !(mask & Button1Mask)) {
		XUngrabPointer(dpy, CurrentTime);
		return;
	}
	if (request) {
		x = request->data.l[0];
		y = request->data.l[1];
	}
	focus(c);
	restack(c->mon);
	movingwin = win = c->win;
	ocx = c->x;
	ocy = c->y;
	ocw = c->w;
	och = c->h;
	anchor = MAX(0.0, MIN(1.0, (double)(x - c->x) / WIDTH(c)));
	offsety = MAX(0, y - c->y);
	while (!XNextEvent(dpy, &ev)) {
		if ((randractive && (ev.type == randrevent + RRScreenChangeNotify
		|| ev.type == randrevent + RRNotify))
		|| (ev.type == ConfigureNotify && ev.xconfigure.window == root)) {
			XPutBackEvent(dpy, &ev);
			break;
		}
		if (ev.type == ButtonRelease && ev.xbutton.button == Button1)
			break;
		if (ev.type == ClientMessage && ev.xclient.window == win
		&& ev.xclient.message_type == netatom[NetWMMoveResize]
		&& ev.xclient.format == 32 && ev.xclient.data.l[2] == 11)
			break;
		if (ev.type == MotionNotify) {
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;
			if (sides) {
				if (!c->hintsvalid)
					updatesizehints(c);
				if (c->isfixed)
					break;
				nw = MAX(1, ocw + ((sides & PlaceLeft) ? x - ev.xmotion.x_root
					: (sides & PlaceRight) ? ev.xmotion.x_root - x : 0));
				nh = MAX(1, och + ((sides & PlaceTop) ? y - ev.xmotion.y_root
					: (sides & PlaceBottom) ? ev.xmotion.y_root - y : 0));
				nx = (sides & PlaceLeft) ? ocx + ocw - nw : ocx;
				ny = (sides & PlaceTop) ? ocy + och - nh : ocy;
				applysizehints(c, &nx, &ny, &nw, &nh, 1);
				/* Size hints must not move the edge opposite the dragged grip. */
				if (sides & PlaceLeft) nx = ocx + ocw - nw;
				if (sides & PlaceTop) ny = ocy + och - nh;
				if (nx == c->x && ny == c->y && nw == c->w && nh == c->h)
					continue;
				c->placement = 0;
				updatestate(c);
				if (!c->isfloating) {
					c->isfloating = 1;
					arrange(c->mon);
				}
				resizeclient(c, nx, ny, nw, nh);
				continue;
			}
			nx = ocx + (ev.xmotion.x_root - x);
			ny = ocy + (ev.xmotion.y_root - y);
			if (nx == ocx && ny == ocy)
				continue;
			if (c->placement) {
				placeclient(c, 0);
				ocx = x - (int)(anchor * WIDTH(c));
				ocy = y - MIN(offsety, HEIGHT(c) - 1);
				nx = ocx + (ev.xmotion.x_root - x);
				ny = ocy + (ev.xmotion.y_root - y);
			}
			m = c->mon;
			if (abs(m->wx - nx) < snap)
				nx = m->wx;
			else if (abs((m->wx + m->ww) - (nx + WIDTH(c))) < snap)
				nx = m->wx + m->ww - WIDTH(c);
			if (abs(m->wy - ny) < snap)
				ny = m->wy;
			else if (abs((m->wy + m->wh) - (ny + HEIGHT(c))) < snap)
				ny = m->wy + m->wh - HEIGHT(c);
			if (!c->isfloating && m->lt[m->sellt]->arrange
			&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap)) {
				c->isfloating = 1;
				arrange(m);
			}
			if (!m->lt[m->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, 1);
		} else if (ev.type != ButtonPress && ev.type >= 0
		&& ev.type < LASTEvent && handler[ev.type]) {
			handler[ev.type](&ev);
			c = wintoclient(win);
			if (!c || c->isdock || c->isfullscreen || !ISVISIBLE(c))
				break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	if ((c = wintoclient(win)) && !c->isfullscreen && ISVISIBLE(c)
	&& (m = recttomon(c->x, c->y, c->w, c->h)) != c->mon) {
		sendmon(c, m);
		focus(c);
	}
	movingwin = None;
}

void
movemouse(const Arg *arg)
{
	dragclient(selmon->sel, NULL);
}

Client *
nexttiled(Client *c)
{
	for (; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

unsigned int
maxaxes(Client *c)
{
	return c->placement == PlaceMax ? PlaceMaxH | PlaceMaxV
		: c->placement & (PlaceMaxH | PlaceMaxV);
}

void
setmaximized(Client *c, unsigned int axes)
{
	if (axes == maxaxes(c))
		return;
	placeclient(c, axes == (PlaceMaxH | PlaceMaxV) ? PlaceMax
		: axes ? axes : c->restoreplacement);
}

void
placeclient(Client *c, unsigned int placement)
{
	Monitor *m = c->mon;
	int x, y, w, h, left, top;
	unsigned int axes, base;

	if (!c->hintsvalid)
		updatesizehints(c);
	if (c->isdock || c->isfullscreen || c->isfixed
	|| m->ww <= 2 * c->bw || m->wh <= 2 * c->bw)
		return;
	if (!c->placement && placement) {
		c->floatx = c->x;
		c->floaty = c->y;
		c->floatw = c->w;
		c->floath = c->h;
	}
	axes = placement == PlaceMax ? PlaceMaxH | PlaceMaxV
		: placement & (PlaceMaxH | PlaceMaxV);
	if (axes && !maxaxes(c))
		c->restoreplacement = c->placement;
	base = axes ? c->restoreplacement : placement;
	c->placement = placement;
	if (!placement) {
		w = MAX(1, MIN(c->floatw, m->ww - 2 * c->bw));
		h = MAX(1, MIN(c->floath, m->wh - 2 * c->bw));
		x = MAX(m->wx, MIN(c->floatx, m->wx + m->ww - w - 2 * c->bw));
		y = MAX(m->wy, MIN(c->floaty, m->wy + m->wh - h - 2 * c->bw));
	} else {
		x = m->wx;
		y = m->wy;
		w = m->ww;
		h = m->wh;
		left = w / 2;
		top = h / 2;
		if (base & PlaceLeft)
			w = left;
		else if (base & PlaceRight) {
			x += left;
			w -= left;
		}
		if (base & PlaceTop)
			h = top;
		else if (base & PlaceBottom) {
			y += top;
			h -= top;
		}
		w = MAX(1, w - 2 * c->bw);
		h = MAX(1, h - 2 * c->bw);
		if (axes) {
			if (!base) {
				x = c->floatx; y = c->floaty;
				w = c->floatw; h = c->floath;
			}
			if (axes & PlaceMaxH) {
				x = m->wx;
				w = m->ww - 2 * c->bw;
			}
			if (axes & PlaceMaxV) {
				y = m->wy;
				h = m->wh - 2 * c->bw;
			}
		}
	}
	c->isfloating = 1;
	resize(c, x, y, w, h, 0);
	updatestate(c);
}

void
pop(Client *c)
{
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void
propertynotify(XEvent *e)
{
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;
	TrayIcon *i = wintotrayicon(ev->window);

	if (i) {
		if (ev->atom == xembedinfo)
			trayiconstate(i);
		updatesystray();
		drawbars();
		return;
	}

	if (ev->window == root && ev->atom == themeatom) {
		if (ev->state != PropertyDelete)
			updatetheme();
		return;
	}
	if (ev->window == root && ev->atom == windowsetupatom) {
		if (ev->state != PropertyDelete)
			updatewindowsetup();
	} else if ((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if (ev->state == PropertyDelete)
		return; /* ignore */
	else if ((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if (!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
				(c->isfloating = (wintoclient(trans)) != NULL))
				arrange(c->mon);
			break;
		case XA_WM_NORMAL_HINTS:
			c->hintsvalid = 0;
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
			drawbars();
			break;
		}
		if (ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			drawbar(c->mon);
			drawtitle(c);
		}
		if (ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg)
{
	running = 0;
}

Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = MAX(h, titleh(c) + 1);
	wc.height = c->h - titleh(c);
	wc.y += titleh(c);
	if (c->ishidden)
		wc.x = -2 * WIDTH(c);
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	if (c->titlebar) {
		unsigned long extents[] = {c->bw, c->bw, c->bw + titleh(c), c->bw};
		XMoveResizeWindow(dpy, c->titlebar, c->x, c->y, MAX(1, c->w), th);
		XSetWindowBorderWidth(dpy, c->titlebar, c->bw);
		XChangeProperty(dpy, c->win, frameatom, XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)extents, 4);
		stacktitle(c);
		drawtitle(c);
	}
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg)
{
	int ocx, ocy, nw, nh;
	Client *c;
	Monitor *m;
	XEvent ev;
	Time lasttime = 0;

	if (movingwin || !(c = selmon->sel))
		return;
	if (!c->hintsvalid)
		updatesizehints(c);
	if (c->isdock || c->isfullscreen || c->isfixed)
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;
	if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
		None, cursor[CurResize]->cursor, CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	c->placement = 0;
	updatestate(c);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			if ((ev.xmotion.time - lasttime) <= (1000 / refreshrate))
				continue;
			lasttime = ev.xmotion.time;

			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if (c->mon->wx + nw >= selmon->wx && c->mon->wx + nw <= selmon->wx + selmon->ww
			&& c->mon->wy + nh >= selmon->wy && c->mon->wy + nh <= selmon->wy + selmon->wh)
			{
				if (!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, 1);
			break;
		}
	} while (ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
	if ((m = recttomon(c->x, c->y, c->w, c->h)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void
restoregeometry(Client *c, const SavedGeometry *g)
{
	Monitor *m;
	int x, y, w, h, border;

	for (m = mons; m && m->num != g->monitor; m = m->next);
	if (!m)
		m = c->mon;
	if (m != c->mon) {
		if (c->mon->sel == c)
			c->mon->sel = NULL;
		detach(c);
		detachstack(c);
		c->mon = m;
		attach(c);
		attachstack(c);
	}
	border = c->isfullscreen ? c->oldbw : c->bw;
	w = MAX(1, MIN(g->w, m->ww - 2 * border));
	h = MAX(1, MIN(g->h, m->wh - 2 * border));
	x = m->wx + MAX(0, MIN(g->x, m->ww - w - 2 * border));
	y = m->wy + MAX(0, MIN(g->y, m->wh - h - 2 * border));
	c->floatx = c->fullx = x;
	c->floaty = c->fully = y;
	c->floatw = c->fullw = w;
	c->floath = c->fullh = h;
	if (c->isfullscreen)
		resizeclient(c, m->mx, m->my, m->mw, m->mh);
	else if (c->placement)
		placeclient(c, c->placement);
	else
		resize(c, x, y, w, h, 0);
}

void
restack(Monitor *m)
{
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar(m);
	if (m->sel && (m->sel->isfloating || !m->lt[m->sellt]->arrange))
		XRaiseWindow(dpy, m->sel->win);
	if (m->lt[m->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = m->barwin;
		for (c = m->stack; c; c = c->snext)
			if (!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	for (c = m->clients; c; c = c->next)
		if (c->isdock && ISVISIBLE(c))
			XRaiseWindow(dpy, c->win);
	for (c = m->clients; c; c = c->next)
		if (c->isabove && ISVISIBLE(c) && !c->isdock)
			XRaiseWindow(dpy, c->win);
	if (m->sel && m->sel->isabove)
		XRaiseWindow(dpy, m->sel->win);
	if (m->showbar)
		XRaiseWindow(dpy, m->barwin);
	if (m->sel && m->sel->isfullscreen)
		XRaiseWindow(dpy, m->sel->win);
	for (c = m->clients; c; c = c->next)
		stacktitle(c);
	if (switchwin)
		XRaiseWindow(dpy, switchwin);
	XSync(dpy, False);
	while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void)
{
	XEvent ev, configure;
	/* main event loop */
	XSync(dpy, False);
	while (running && !XNextEvent(dpy, &ev)) {
		if (randractive && (ev.type == randrevent + RRScreenChangeNotify
		|| ev.type == randrevent + RRNotify)) {
			XRRUpdateConfiguration(&ev);
			configure = (XEvent){0};
			configure.xconfigure.window = root;
			configure.xconfigure.width = DisplayWidth(dpy, screen);
			configure.xconfigure.height = DisplayHeight(dpy, screen);
			configurenotify(&configure);
		} else if (ev.type >= 0 && ev.type < LASTEvent && handler[ev.type])
			handler[ev.type](&ev); /* call handler */
	}
}

void
savewindowgeometry(void)
{
	Client *c;
	Monitor *m;
	SavedGeometry *g;
	unsigned int i;
	int x, y, w, h;

	if (!windowsetup[0])
		return;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next) {
			if (c->isdock || !c->isfloating || (c->isfullscreen && !c->oldstate))
				continue;
			for (i = 0; i < c->geometrycount && strcmp(c->geometry[i].setup, windowsetup); i++);
			if (i == c->geometrycount) {
				if (i < SavedSetups)
					c->geometrycount++;
				else {
					i = c->nextgeometry;
					c->nextgeometry = (i + 1) % SavedSetups;
				}
			}
			if (c->placement) {
				x = c->floatx; y = c->floaty; w = c->floatw; h = c->floath;
			} else if (c->isfullscreen) {
				x = c->fullx; y = c->fully; w = c->fullw; h = c->fullh;
			} else {
				x = c->x; y = c->y; w = c->w; h = c->h;
			}
			g = &c->geometry[i];
			memcpy(g->setup, windowsetup, sizeof g->setup);
			g->monitor = m->num;
			g->x = x - m->wx; g->y = y - m->wy;
			g->w = MAX(1, w); g->h = MAX(1, h);
		}
}

void
scan(void)
{
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for (i = 0; i < num; i++) {
			if (!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for (i = 0; i < num; i++) { /* now the transients */
			if (!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if (XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if (wins)
			XFree(wins);
	}
}

void
sendmon(Client *c, Monitor *m)
{
	int dx, dy;

	if (c->mon == m)
		return;
	dx = m->wx - c->mon->wx;
	dy = m->wy - c->mon->wy;
	c->floatx += dx;
	c->floaty += dy;
	c->fullx += dx;
	c->fully += dy;
	unfocus(c, 1);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	if (c->isfullscreen)
		resizeclient(c, m->mx, m->my, m->mw, m->mh);
	else if (c->placement)
		placeclient(c, c->placement);
	else if (c->win != movingwin)
		resize(c, c->x + dx, c->y + dy, c->w, c->h, 0);
	focus(NULL);
	arrange(NULL);
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c)
{
	if (!c->neverfocus)
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	XChangeProperty(dpy, root, netatom[NetActiveWindow], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *)&c->win, 1);
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		c->isfullscreen = 1;
		updatestate(c);
		c->fullx = c->x;
		c->fully = c->y;
		c->fullw = c->w;
		c->fullh = c->h;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
		c->isfullscreen = 0;
		updatestate(c);
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		resizeclient(c, c->fullx, c->fully, c->fullw, c->fullh);
		if (c->placement)
			placeclient(c, c->placement);
		arrange(c->mon);
	}
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if (selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.05 || f > 0.95)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setup(void)
{
	int i, error, major = 1, minor = 3;
	XSetWindowAttributes wa;
	Atom utf8string;
	struct sigaction sa;

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from .xinitrc etc) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	bh = drw->fonts->h + 2;
	th = showtitlebar ? MAX(24, bh) : 0;
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	themeatom = XInternAtom(dpy, "_WM_THEME", False);
	motifatom = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
	csdatom = XInternAtom(dpy, "_GTK_FRAME_EXTENTS", False);
	frameatom = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
	windowsetupatom = XInternAtom(dpy, "_WM_DISPLAY_SETUP", False);
	statuspidatom = XInternAtom(dpy, "_DWM_BLOCKS_PID", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmchangeatom = XInternAtom(dpy, "WM_CHANGE_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetWMWindowTypeDock] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	netatom[NetWMHidden] = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
	netatom[NetWMAbove] = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
	netatom[NetWMMaxH] = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	netatom[NetWMMaxV] = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	netatom[NetWMMoveResize] = XInternAtom(dpy, "_NET_WM_MOVERESIZE", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	inittitlecolors(!strcmp(colors[SchemeSel][ColBg], "#7e9ebd"));
	randractive = XRRQueryExtension(dpy, &randrevent, &error)
		&& XRRQueryVersion(dpy, &major, &minor)
		&& (major > 1 || (major == 1 && minor >= 3));
	if (randractive)
		XRRSelectInput(dpy, root, RRScreenChangeNotifyMask|RRCrtcChangeNotifyMask|RROutputChangeNotifyMask);
	/* init bars before embedding the tray */
	updatebars();
	updateprimary();
	setuptray();
	updatestatus();
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	focus(NULL);
}

void
seturgent(Client *c, int urg)
{
	XWMHints *wmh;

	c->isurgent = urg;
	if (!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags = urg ? (wmh->flags | XUrgencyHint) : (wmh->flags & ~XUrgencyHint);
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
showhide(Client *c)
{
	if (!c)
		return;
	if (ISVISIBLE(c)) {
		/* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y + titleh(c));
		stacktitle(c);
		if ((!c->mon->lt[c->mon->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, 0);
		showhide(c->snext);
	} else {
		/* hide clients bottom up */
		showhide(c->snext);
		if (c->titlebar)
			XUnmapWindow(dpy, c->titlebar);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
snapclient(const Arg *arg)
{
	Client *c = selmon->sel;
	unsigned int axis, placement;

	if (!c || c->isfullscreen)
		return;
	switch (arg->ui) {
	case PlaceLeft:
	case PlaceRight:
		axis = PlaceLeft | PlaceRight;
		break;
	case PlaceTop:
	case PlaceBottom:
		axis = PlaceTop | PlaceBottom;
		break;
	default:
		return;
	}
	placement = maxaxes(c) ? 0 : c->placement;
	placement = (placement & arg->ui) ? placement & ~axis
		: (placement & ~axis) | arg->ui;
	placeclient(c, placement);
}

void
spawn(const Arg *arg)
{
	struct sigaction sa;
	pid_t pid;
	char **argv;
	size_t n;

	if (arg->v == dmenucmd || arg->v == commandcmd)
		snprintf(dmenumon, sizeof(dmenumon), "%d", selmon->num);

	if ((pid = fork()) == -1) {
		fprintf(stderr, "dwm: fork failed: %s\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		for (n = 0; ((char **)arg->v)[n]; n++);
		argv = ecalloc(n + 3, sizeof *argv);
		argv[0] = "wm-run";
		argv[1] = "--";
		memcpy(argv + 2, arg->v, (n + 1) * sizeof *argv);
		execvp(argv[0], argv);
		fprintf(stderr, "dwm: execvp wm-run failed: %s\n", strerror(errno));
		execlp("xmessage", "xmessage", "-center", "-timeout", "15",
			"wm: no se pudo iniciar wm-run. Ejecuta el instalador de wm.", (char *)NULL);
		die("dwm: launch error could not be displayed:");
	}
}

void
tag(const Arg *arg)
{
	if (selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	Client *c = selmon->sel;
	Monitor *m;

	if (!c || !mons->next)
		return;
	m = dirtomon(arg->i);
	sendmon(c, m);
	focus(c);
	restack(m);
}

void
tile(Monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->ww * m->mfact : 0;
	else
		mw = m->ww;
	for (i = my = ty = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), i++)
		if (i < m->nmaster) {
			h = (m->wh - my) / (MIN(n, m->nmaster) - i);
			resize(c, m->wx, m->wy + my, mw - (2*c->bw), h - (2*c->bw), 0);
			if (my + HEIGHT(c) < m->wh)
				my += HEIGHT(c);
		} else {
			h = (m->wh - ty) / (n - i);
			resize(c, m->wx + mw, m->wy + ty, m->ww - mw - (2*c->bw), h - (2*c->bw), 0);
			if (ty + HEIGHT(c) < m->wh)
				ty += HEIGHT(c);
		}
}

void
togglebar(const Arg *arg)
{
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	XMoveResizeWindow(dpy, selmon->barwin, selmon->wx, selmon->by, selmon->ww, bh);
	arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
	if (!selmon->sel)
		return;
	if (selmon->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if (selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
			selmon->sel->w, selmon->sel->h, 0);
	arrange(selmon);
}

void
togglefullscreen(const Arg *arg)
{
	if (selmon->sel)
		setfullscreen(selmon->sel, !selmon->sel->isfullscreen);
}

void
togglemaximize(const Arg *arg)
{
	Client *c = selmon->sel;

	if (c && !c->isfullscreen)
		placeclient(c, c->placement == PlaceMax
			? c->restoreplacement : PlaceMax);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
		arrange(selmon);
	}
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	grabbuttons(c, 0);
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, int destroyed)
{
	Monitor *m = c->mon;
	XWindowChanges wc;
	size_t i;

	if (switchmenu && switchtarget == c->win)
		endswitcher(0);
	for (i = 0; !switchmenu && i < switchcount; i++)
		if (switchitems[i] == c->win) {
			memmove(switchitems + i, switchitems + i + 1,
				(--switchcount - i) * sizeof(Window));
			if (!switchcount)
				endswitcher(0);
			else {
				if (i < switchindex)
					switchindex--;
				switchindex %= switchcount;
				drawswitcher();
			}
			break;
		}
	detach(c);
	detachstack(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XSelectInput(dpy, c->win, NoEventMask);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		if (c->ishidden)
			XMoveWindow(dpy, c->win, c->x, c->y + titleh(c));
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	if (c->titlebar) {
		XDestroyWindow(dpy, c->titlebar);
		if (!destroyed)
			XDeleteProperty(dpy, c->win, frameatom);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange(m);
}

void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;
	TrayIcon *i = wintotrayicon(ev->window);

	if (i) {
		trayiconstate(i);
		updatesystray();
		drawbars();
		return;
	}
	if ((c = wintoclient(ev->window))) {
		if (c->ignoreunmaps && !ev->send_event) {
			c->ignoreunmaps--;
			return;
		}
		if (ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);
	}
}

void
updatebars(void)
{
	Monitor *m;
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	XClassHint ch = {"dwm", "dwm"};
	for (m = mons; m; m = m->next) {
		if (m->barwin)
			continue;
		m->barwin = XCreateWindow(dpy, root, m->wx, m->by, m->ww, bh, 0, DefaultDepth(dpy, screen),
				CopyFromParent, DefaultVisual(dpy, screen),
				CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
		XDefineCursor(dpy, m->barwin, cursor[CurNormal]->cursor);
		XMapRaised(dpy, m->barwin);
		XSetClassHint(dpy, m->barwin, &ch);
	}
}

void
updatebarpos(Monitor *m)
{
	m->wy = m->my;
	m->wh = m->mh;
	if (m->showbar) {
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	} else
		m->by = -bh;
}

void
updateclientlist(void)
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;

		/* new monitors if nn > n */
		for (i = n; i < nn; i++) {
			for (m = mons; m && m->next; m = m->next);
			if (m)
				m->next = createmon();
			else
				mons = createmon();
		}
		for (i = 0, m = mons; i < nn && m; m = m->next, i++)
			if (i >= n
			|| unique[i].x_org != m->mx || unique[i].y_org != m->my
			|| unique[i].width != m->mw || unique[i].height != m->mh)
			{
				dirty = 1;
				m->num = i;
				m->mx = m->wx = unique[i].x_org;
				m->my = m->wy = unique[i].y_org;
				m->mw = m->ww = unique[i].width;
				m->mh = m->wh = unique[i].height;
				updatebarpos(m);
			}
		/* removed monitors if n > nn */
		for (i = nn; i < n; i++) {
			for (m = mons; m && m->next; m = m->next);
			while ((c = m->clients)) {
				dirty = 1;
				m->clients = c->next;
				detachstack(c);
				c->mon = mons;
				attach(c);
				attachstack(c);
			}
			if (m == selmon)
				selmon = mons;
			cleanupmon(m);
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void
updatenumlockmask(void)
{
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	if (!(modmap = XGetModifierMapping(dpy)))
		return;
	for (i = 0; i < 8; i++)
		for (j = 0; j < modmap->max_keypermod; j++)
			if (modmap->modifiermap[i * modmap->max_keypermod + j]
				== XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c)
{
	long msize;
	XSizeHints size;

	if (!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if (size.flags & PBaseSize) {
		c->basew = MAX(0, size.base_width);
		c->baseh = MAX(0, size.base_height);
	} else if (size.flags & PMinSize) {
		c->basew = MAX(0, size.min_width);
		c->baseh = MAX(0, size.min_height);
	} else
		c->basew = c->baseh = 0;
	if (size.flags & PResizeInc) {
		c->incw = MAX(0, size.width_inc);
		c->inch = MAX(0, size.height_inc);
	} else
		c->incw = c->inch = 0;
	if (size.flags & PMaxSize) {
		c->maxw = MAX(0, size.max_width);
		c->maxh = MAX(0, size.max_height);
	} else
		c->maxw = c->maxh = 0;
	if (size.flags & PMinSize) {
		c->minw = MAX(0, size.min_width);
		c->minh = MAX(0, size.min_height);
	} else if (size.flags & PBaseSize) {
		c->minw = MAX(0, size.base_width);
		c->minh = MAX(0, size.base_height);
	} else
		c->minw = c->minh = 0;
	if ((size.flags & PAspect) && size.min_aspect.x > 0 && size.max_aspect.y > 0) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	} else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->maxh && c->maxw == c->minw && c->maxh == c->minh);
	c->hintsvalid = 1;
}

unsigned int
statusblockat(int x)
{
	char text[sizeof statusraw];
	size_t i, n = 0;
	unsigned int id = 0, width;

	if (x < 0)
		return 0;
	for (i = 0; ; i++) {
		if ((unsigned char)statusraw[i] < 32) {
			text[n] = '\0';
			width = drw_fontset_getwidth(drw, text);
			if ((unsigned int)x < width)
				return id;
			x -= width;
			if (!statusraw[i])
				return 0;
			id = (unsigned char)statusraw[i];
			n = 0;
		} else if ((unsigned char)statusraw[i] != 127)
			text[n++] = statusraw[i];
	}
}

void
statusclick(int x, unsigned int button)
{
	Atom actual;
	int format;
	unsigned long count, remaining, pid;
	unsigned char *data = NULL;
	unsigned int id = statusblockat(x);
	union sigval value;

	if (!id || button < 1 || button > 9)
		return;
	if (XGetWindowProperty(dpy, root, statuspidatom, 0, 1, False, XA_CARDINAL,
		&actual, &format, &count, &remaining, &data) != Success)
		return;
	if (data && actual == XA_CARDINAL && format == 32 && count == 1 && !remaining) {
		pid = *(unsigned long *)data;
		if (pid > 1 && pid <= 0x7fffffffUL) {
			value.sival_int = (id << 8) | button;
			if (sigqueue((pid_t)pid, SIGUSR1, value) == -1 && errno != ESRCH)
				fprintf(stderr, "dwm: status signal failed: %s\n", strerror(errno));
		}
	}
	if (data)
		XFree(data);
}

void
updatestatus(void)
{
	size_t i, n = 0;

	if (!gettextprop(root, XA_WM_NAME, statusraw, sizeof statusraw))
		strcpy(statusraw, "dwm-"VERSION);
	/* Block IDs are protocol bytes, never glyphs for Xft to discover. */
	for (i = 0; statusraw[i]; i++)
		if ((unsigned char)statusraw[i] >= 32 && (unsigned char)statusraw[i] != 127)
			stext[n++] = statusraw[i];
	stext[n] = '\0';
	if (statusmon)
		drawbar(statusmon);
}

void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void
updatewindowtype(Client *c)
{
	Atom actual = None, wtype = getatomprop(c, netatom[NetWMWindowType]);
	unsigned char *data = NULL;
	unsigned long n = 0, extra, i;
	unsigned int axes = 0;
	int format = 0, fullscreen = 0;

	if (XGetWindowProperty(dpy, c->win, netatom[NetWMState], 0, 1024,
		False, XA_ATOM, &actual, &format, &n, &extra, &data) == Success
	&& actual == XA_ATOM && format == 32 && !extra)
		for (i = 0; i < n; i++) {
			if (((Atom *)data)[i] == netatom[NetWMFullscreen])
				fullscreen = 1;
			if (((Atom *)data)[i] == netatom[NetWMMaxH])
				axes |= PlaceMaxH;
			if (((Atom *)data)[i] == netatom[NetWMMaxV])
				axes |= PlaceMaxV;
		}
	if (data)
		XFree(data);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
	if (wtype == netatom[NetWMWindowTypeDock]) {
		c->isdock = c->isfloating = c->neverfocus = 1;
		c->bw = 0;
		XSetWindowBorderWidth(dpy, c->win, 0);
	}
	if (!c->isdock) {
		if (fullscreen)
			setfullscreen(c, 1);
		if (axes && c->hintsvalid)
			setmaximized(c, axes);
	}
}

static int
themevalues(const char *value, char names[2][3][8])
{
	unsigned int i, j;

	if (strlen(value) != 47)
		return 0;
	for (i = 0; i < 6; i++) {
		if (value[i * 8] != '#' || (i < 5 && value[i * 8 + 7] != ' '))
			return 0;
		for (j = 1; j < 7; j++)
			if (!isxdigit((unsigned char)value[i * 8 + j]))
				return 0;
	}
	for (i = 0; i < 6; i++) {
		memcpy(names[i / 3][i % 3], value + i * 8, 7);
		names[i / 3][i % 3][7] = '\0';
	}
	return 1;
}

void
updatetheme(void)
{
	Atom actual, utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	int format;
	unsigned int i, j;
	unsigned long count, remaining;
	unsigned char *data = NULL;
	char names[2][3][8];
	const char *row[3];
	Clr *updated[2];
	Monitor *m;
	Client *c;

	if (XGetWindowProperty(dpy, root, themeatom, 0, 12, False, utf8string,
		&actual, &format, &count, &remaining, &data) != Success)
		return;
	if (!data || actual != utf8string || format != 8 || count != 47
	|| remaining || !themevalues((char *)data, names)) {
		if (data)
			XFree(data);
		return;
	}
	XFree(data);
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++)
			row[j] = names[i][j];
		updated[i] = drw_scm_create(drw, row, 3);
	}
	for (i = 0; i < 2; i++) {
		for (j = 0; j < 3; j++)
			XftColorFree(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), &scheme[i][j]);
		free(scheme[i]);
		scheme[i] = updated[i];
	}
	inittitlecolors(!strcmp(names[SchemeSel][ColBg], "#7e9ebd"));
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (!c->isdock)
				XSetWindowBorder(dpy, c->win, scheme[c == selmon->sel ? SchemeSel : SchemeNorm][ColBorder].pixel);
	if (traywin)
		XSetWindowBackground(dpy, traywin, scheme[SchemeNorm][ColBg].pixel);
	updatesystray();
	drawbars();
}

void
updatewindowsetup(void)
{
	Atom actual;
	int format, valid = 1;
	unsigned long length, remaining;
	unsigned char *data = NULL;
	char key[SetupLength + 1];
	Client *c, *active = selmon->sel, **clients;
	Monitor *m;
	size_t i, j, count = 0;

	if (XGetWindowProperty(dpy, root, windowsetupatom, 0, (SetupLength + 4) / 4,
		False, XA_STRING, &actual, &format, &length, &remaining, &data) != Success || !data)
		return;
	if (actual != XA_STRING || format != 8 || remaining)
		valid = 0;
	else if (length == 7 && !memcmp(data, "pending", 7)) {
		XFree(data);
		beginwindowsetup();
		return;
	} else if (length != SetupLength)
		valid = 0;
	else
		for (i = 0; i < SetupLength; i++)
			if (!((data[i] >= '0' && data[i] <= '9') || (data[i] >= 'a' && data[i] <= 'f')))
				valid = 0;
	if (valid) {
		memcpy(key, data, SetupLength);
		key[SetupLength] = '\0';
	}
	XFree(data);
	if (!valid || (!setuppending && !strcmp(key, windowsetup)))
		return;
	if (!setuppending)
		savewindowgeometry();
	memcpy(windowsetup, key, sizeof windowsetup);
	setuppending = 0;
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			count++;
	if (!count)
		return;
	clients = ecalloc(count, sizeof *clients);
	for (i = 0, m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			clients[i++] = c;
	for (i = 0; i < count; i++) {
		c = clients[i];
		if (c->isdock || !c->isfloating || (c->isfullscreen && !c->oldstate))
			continue;
		for (j = 0; j < c->geometrycount; j++)
			if (!strcmp(c->geometry[j].setup, key)) {
				restoregeometry(c, &c->geometry[j]);
				break;
			}
	}
	free(clients);
	if (active)
		focus(active);
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focus(NULL);
	arrange(selmon);
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}

Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (statusmon && (w == traywin || wintotrayicon(w)))
		return statusmon;
	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)) || (c = titleclient(w)))
		return c->mon;
	return selmon;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "dwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	die("dwm: another window manager is already running");
	return -1;
}

void
zoom(const Arg *arg)
{
	Client *c = selmon->sel;

	if (!selmon->lt[selmon->sellt]->arrange || !c || c->isfloating)
		return;
	if (c == nexttiled(selmon->clients) && !(c = nexttiled(c->next)))
		return;
	pop(c);
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION);
	else if (argc != 1)
		die("usage: dwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("dwm: cannot open display");
	checkotherwm();
	setup();
#ifdef __OpenBSD__
	if (pledge("stdio rpath proc exec", NULL) == -1)
		die("pledge");
#endif /* __OpenBSD__ */
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
