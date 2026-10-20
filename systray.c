/* See LICENSE file. XEmbed adapted from the suckless systray patch (Bakkeby).
 * Original and provenance: coordinator patches/upstream/dwm/ and patches/README.md.
 */
typedef struct TrayIcon TrayIcon;
struct TrayIcon {
	Window win;
	int w, h, mapped;
	TrayIcon *next;
};

static Window traywin;
static TrayIcon *trayicons;
static Monitor *statusmon, *traymon;
static Atom trayatom, trayopcode, trayorientation, manageratom, xembed, xembedinfo;
static Atom trayready;
static int randractive, randrevent;

static void
traymessage(Window win, long message, long detail, long data1, long data2)
{
	XEvent ev = {0};

	ev.xclient.type = ClientMessage;
	ev.xclient.window = win;
	ev.xclient.message_type = xembed;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = message;
	ev.xclient.data.l[2] = detail;
	ev.xclient.data.l[3] = data1;
	ev.xclient.data.l[4] = data2;
	XSendEvent(dpy, win, False, NoEventMask, &ev);
}

static TrayIcon *
wintotrayicon(Window win)
{
	TrayIcon *i;

	for (i = trayicons; i && i->win != win; i = i->next);
	return i;
}

static void
trayicongeom(TrayIcon *i, int w, int h)
{
	double width;

	width = w > 0 && h > 0 ? (double)bh * w / h : bh;
	i->w = MAX(1, MIN(width, 4.0 * bh));
	i->h = bh;
}

static void
trayiconstate(TrayIcon *i)
{
	Atom actual;
	int format, mapped = i->mapped;
	unsigned long count, remaining;
	unsigned char *data = NULL;

	if (XGetWindowProperty(dpy, i->win, xembedinfo, 0, 2, False,
		xembedinfo, &actual, &format, &count, &remaining, &data) == Success
	&& data && actual == xembedinfo && format == 32 && count == 2 && !remaining)
		mapped = !!(((unsigned long *)data)[1] & 1); /* XEMBED_MAPPED */
	if (data)
		XFree(data);
	if (mapped != i->mapped) {
		i->mapped = mapped;
		traymessage(i->win, mapped ? 1 : 2, 0, 0, 0);
	}
}

static int
gettraywidth(Monitor *m)
{
	TrayIcon *i;
	int w = 0, limit = MAX(0, m->ww - 1);

	if (!traywin || m != statusmon || !m->showbar)
		return 0;
	for (i = trayicons; i; i = i->next)
		if (i->mapped)
			w = MIN(limit, w + i->w + 2);
	return w ? MIN(limit, w + 2) : 0;
}

static void
updatesystray(void)
{
	TrayIcon *i;
	Monitor *m = statusmon;
	int x = 2, w;

	if (!traywin || !m || !m->barwin)
		return;
	w = gettraywidth(m);
	if (traymon != m) {
		XReparentWindow(dpy, traywin, m->barwin, m->ww - MAX(1, w), 0);
		traymon = m;
	}
	XMoveResizeWindow(dpy, traywin, m->ww - MAX(1, w), 0, MAX(1, w), bh);
	for (i = trayicons; i; i = i->next) {
		if (!i->mapped) {
			XUnmapWindow(dpy, i->win);
			continue;
		}
		XMoveResizeWindow(dpy, i->win, MIN(x, MAX(1, w)), 0, i->w, i->h);
		XMapWindow(dpy, i->win);
		x = MIN(MAX(1, w), x + i->w + 2);
	}
	if (w)
		XMapRaised(dpy, traywin);
	else
		XUnmapWindow(dpy, traywin);
}

static void
removetrayicon(TrayIcon *i, int destroyed)
{
	TrayIcon **p;

	for (p = &trayicons; *p && *p != i; p = &(*p)->next);
	if (!*p)
		return;
	*p = i->next;
	if (!destroyed) {
		XSelectInput(dpy, i->win, NoEventMask);
		XReparentWindow(dpy, i->win, root, 0, 0);
		XRemoveFromSaveSet(dpy, i->win);
	}
	free(i);
}

static void
cleanuptray(void)
{
	while (trayicons)
		removetrayicon(trayicons, 0);
	if (traywin) {
		XDeleteProperty(dpy, root, trayready);
		XDestroyWindow(dpy, traywin);
	}
	traywin = None;
	traymon = NULL;
}

static void
docktrayicon(Window win)
{
	XWindowAttributes wa;
	TrayIcon *i;
	Monitor *m;

	if (!win || win == root || win == traywin || win == wmcheckwin
	|| wintotrayicon(win) || wintoclient(win))
		return;
	for (m = mons; m; m = m->next)
		if (win == m->barwin)
			return;
	if (!XGetWindowAttributes(dpy, win, &wa) || wa.class == InputOnly)
		return;
	i = ecalloc(1, sizeof *i);
	i->win = win;
	i->mapped = 1;
	trayicongeom(i, wa.width, wa.height);
	i->next = trayicons;
	trayicons = i;
	trayiconstate(i);
	XAddToSaveSet(dpy, win);
	XSetWindowBorderWidth(dpy, win, 0);
	XSelectInput(dpy, win, StructureNotifyMask|PropertyChangeMask|ResizeRedirectMask);
	XReparentWindow(dpy, win, traywin, 0, 0);
	traymessage(win, 0, 0, traywin, 0); /* XEMBED_EMBEDDED_NOTIFY, version 0 */
	updatesystray();
	drawbars();
}

static void
updateprimary(void)
{
	XRRScreenResources *resources;
	XRROutputInfo *output;
	XRRCrtcInfo *crtc;
	RROutput primary;
	Monitor *m;

	statusmon = mons;
	if (randractive && (primary = XRRGetOutputPrimary(dpy, root)) != None) {
		resources = XRRGetScreenResourcesCurrent(dpy, root);
		if (resources) {
			output = XRRGetOutputInfo(dpy, resources, primary);
			if (output) {
				if (output->connection == RR_Connected && output->crtc) {
					crtc = XRRGetCrtcInfo(dpy, resources, output->crtc);
					if (crtc) {
						for (m = mons; m; m = m->next)
							if (crtc->width && crtc->height && m->mx == crtc->x && m->my == crtc->y) {
								statusmon = m;
								break;
							}
						XRRFreeCrtcInfo(crtc);
					}
				}
				XRRFreeOutputInfo(output);
			}
			XRRFreeScreenResources(resources);
		}
	}
	updatesystray();
}

static void
setuptray(void)
{
	char name[64];
	unsigned long ready[2];
	long orientation = 0;
	XEvent ev = {0};
	XSetWindowAttributes wa = { .override_redirect = True,
		.event_mask = SubstructureNotifyMask,
		.background_pixel = scheme[SchemeNorm][ColBg].pixel };

	snprintf(name, sizeof name, "_NET_SYSTEM_TRAY_S%d", screen);
	trayatom = XInternAtom(dpy, name, False);
	trayopcode = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
	trayorientation = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
	manageratom = XInternAtom(dpy, "MANAGER", False);
	xembed = XInternAtom(dpy, "_XEMBED", False);
	xembedinfo = XInternAtom(dpy, "_XEMBED_INFO", False);
	trayready = XInternAtom(dpy, "_WM_SYSTRAY", False);
	XGrabServer(dpy);
	if (XGetSelectionOwner(dpy, trayatom) != None) {
		XUngrabServer(dpy);
		fputs("dwm: system tray already owned; leaving it untouched\n", stderr);
		return;
	}
	traywin = XCreateWindow(dpy, root, 0, 0, 1, bh, 0, DefaultDepth(dpy, screen),
		CopyFromParent, DefaultVisual(dpy, screen),
		CWOverrideRedirect|CWBackPixel|CWEventMask, &wa);
	XChangeProperty(dpy, traywin, trayorientation, XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)&orientation, 1);
	XSetSelectionOwner(dpy, trayatom, traywin, CurrentTime);
	if (XGetSelectionOwner(dpy, trayatom) != traywin) {
		XUngrabServer(dpy);
		cleanuptray();
		fputs("dwm: unable to obtain system tray\n", stderr);
		return;
	}
	XUngrabServer(dpy);
	ready[0] = getpid();
	ready[1] = traywin;
	XChangeProperty(dpy, root, trayready, XA_CARDINAL, 32,
		PropModeReplace, (unsigned char *)ready, 2);
	ev.xclient.type = ClientMessage;
	ev.xclient.window = root;
	ev.xclient.message_type = manageratom;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = CurrentTime;
	ev.xclient.data.l[1] = trayatom;
	ev.xclient.data.l[2] = traywin;
	XSendEvent(dpy, root, False, StructureNotifyMask, &ev);
	updatesystray();
}

static void
resizerequest(XEvent *e)
{
	TrayIcon *i = wintotrayicon(e->xresizerequest.window);

	if (!i)
		return;
	trayicongeom(i, e->xresizerequest.width, e->xresizerequest.height);
	updatesystray();
	drawbars();
}

static void
selectionclear(XEvent *e)
{
	if (e->xselectionclear.selection != trayatom)
		return;
	cleanuptray();
	drawbars();
}
