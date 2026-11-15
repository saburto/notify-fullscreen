/*
 * X11 / Xwayland backend.
 *
 * One 32-bit ARGB window covering the primary screen, EWMH fullscreen +
 * above, cairo painting into it. Works under Xwayland on Wayland sessions.
 */

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <sys/select.h>
#include <unistd.h>

#include "backend.h"

struct backend {
    Display *dpy;
    Window root;
    Window win;
    Visual *visual;
    int argb; /* window has an alpha channel => real transparency */
    Colormap cmap;
    Cursor c_blank;
    Cursor c_hand;
    Cursor c_current;
    Window prev_active; /* window that was active before we mapped */
};

/* X errors are not fatal here: focus/grab races happen under Xwayland. */
static int on_x_error(Display *dpy, XErrorEvent *ev)
{
    (void)dpy;
    (void)ev;
    return 0;
}

static Cursor make_blank_cursor(Display *dpy)
{
    static const char bits[1] = {0};
    Pixmap pm = XCreateBitmapFromData(dpy, DefaultRootWindow(dpy), bits, 1, 1);
    XColor dummy;
    memset(&dummy, 0, sizeof dummy);
    Cursor c = XCreatePixmapCursor(dpy, pm, pm, &dummy, &dummy, 0, 0);
    XFreePixmap(dpy, pm);
    return c;
}

/* Ask the window manager to activate `target` (EWMH _NET_ACTIVE_WINDOW). */
static void ask_wm_to_activate(Display *dpy, Window root, Window target)
{
    XEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = target;
    ev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2; /* source: pager — allowed to steal focus */
    ev.xclient.data.l[1] = CurrentTime;
    ev.xclient.data.l[2] = None;
    XSendEvent(dpy, root, False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

/* The window that had the focus before we appeared (--refocus). */
static Window active_window_before(Display *dpy, Window root)
{
    Atom act = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    Atom type = None;
    int fmt = 0;
    unsigned long n = 0, after = 0;
    unsigned char *data = NULL;
    Window w = None;

    if (XGetWindowProperty(dpy, root, act, 0, 1, False, XA_WINDOW, &type,
                           &fmt, &n, &after, &data) == Success &&
        data) {
        if (fmt == 32 && n == 1)
            w = (Window)((long *)data)[0];
        XFree(data);
    }

    if (!w) {
        /* no EWMH state, try the plain X input focus */
        int revert = 0;
        Window f = None;
        XGetInputFocus(dpy, &f, &revert);
        if (f != None && f != PointerRoot && f != root)
            w = f;
    }
    return w;
}

static void request_focus(struct backend *be)
{
    ask_wm_to_activate(be->dpy, be->root, be->win);

    XWindowAttributes wa;
    if (!XGetWindowAttributes(be->dpy, be->win, &wa) ||
        wa.map_state != IsViewable)
        return; /* XSetInputFocus/XGrabKeyboard would raise BadMatch */

    XSetInputFocus(be->dpy, be->win, RevertToPointerRoot, CurrentTime);
    XGrabKeyboard(be->dpy, be->win, True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
}

static void set_atoms(struct backend *be)
{
    Atom state = XInternAtom(be->dpy, "_NET_WM_STATE", False);
    Atom vals[3] = {
        XInternAtom(be->dpy, "_NET_WM_STATE_FULLSCREEN", False),
        XInternAtom(be->dpy, "_NET_WM_STATE_ABOVE", False),
        XInternAtom(be->dpy, "_NET_WM_STATE_SKIP_TASKBAR", False),
    };
    XChangeProperty(be->dpy, be->win, state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)vals, 3);

    Atom type = XInternAtom(be->dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom normal = XInternAtom(be->dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    XChangeProperty(be->dpy, be->win, type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&normal, 1);

    /* keep the notification out of the WM's minimise/close UI */
    Atom pid = XInternAtom(be->dpy, "_NET_WM_PID", False);
    long pidval = (long)getpid();
    XChangeProperty(be->dpy, be->win, pid, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&pidval, 1);

    XStoreName(be->dpy, be->win, "notify-fullscreen");
    XClassHint hint = {(char *)"notify-fullscreen",
                       (char *)"Notify-fullscreen"};
    XSetClassHint(be->dpy, be->win, &hint);
}

/* map a keysym onto a core action */
static int translate_key(KeySym ks, nf_key *out)
{
    switch (ks) {
    case XK_Return:
    case XK_KP_Enter:
    case XK_space:
        *out = NF_KEY_ACTIVATE;
        return 1;
    case XK_Escape:
        *out = NF_KEY_CANCEL;
        return 1;
    case XK_y:
    case XK_Y:
        *out = NF_KEY_YES;
        return 1;
    case XK_n:
    case XK_N:
        *out = NF_KEY_NO;
        return 1;
    case XK_q:
        *out = NF_KEY_QUIT;
        return 1;
    case XK_Tab:
        *out = NF_KEY_TAB;
        return 1;
    case XK_Left:
        *out = NF_KEY_LEFT;
        return 1;
    case XK_Right:
        *out = NF_KEY_RIGHT;
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------- interface */

struct backend *nf_backend_new(void)
{
    return nf_malloc(sizeof(struct backend));
}

void nf_bm_open(struct backend *be)
{
    memset(be, 0, sizeof *be);
    be->dpy = XOpenDisplay(NULL);
    if (!be->dpy)
        nf_die("cannot open X display (is DISPLAY set? "
               "is Xwayland/X11 running?)");
    be->root = DefaultRootWindow(be->dpy);
    XSetErrorHandler(on_x_error);
}

void nf_bm_close(struct backend *be)
{
    if (be->dpy)
        XCloseDisplay(be->dpy);
    free(be);
}

void nf_bm_screen_size(struct backend *be, int *w, int *h)
{
    const int screen = DefaultScreen(be->dpy);
    *w = DisplayWidth(be->dpy, screen);
    *h = DisplayHeight(be->dpy, screen);
}

void nf_bm_create_window(struct backend *be, nf_app *a)
{
    const int screen = DefaultScreen(be->dpy);

    /* pick a 32-bit ARGB visual so the background can be translucent */
    XVisualInfo vinfo;
    if (XMatchVisualInfo(be->dpy, screen, 32, TrueColor, &vinfo)) {
        be->visual = vinfo.visual;
        be->argb = 1;
    } else {
        be->visual = DefaultVisual(be->dpy, screen);
        be->argb = 0;
    }
    a->translucent = be->argb;

    XSetWindowAttributes at;
    memset(&at, 0, sizeof at);
    at.border_pixel = 0;
    at.background_pixel = 0;
    at.background_pixmap = None;
    at.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                    PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    if (be->argb) {
        be->cmap = XCreateColormap(be->dpy, be->root, be->visual, AllocNone);
        at.colormap = be->cmap;
    }

    be->win = XCreateWindow(be->dpy, be->root, 0, 0, (unsigned)a->w,
                            (unsigned)a->h, 0,
                            be->argb ? 32 : DefaultDepth(be->dpy, screen),
                            InputOutput, be->visual,
                            CWColormap | CWBorderPixel | CWBackPixel |
                                CWBackPixmap | CWEventMask,
                            &at);
    set_atoms(be);

    be->c_blank = make_blank_cursor(be->dpy);
    be->c_hand = XCreateFontCursor(be->dpy, XC_hand2);
    be->c_current = be->c_blank;
    XDefineCursor(be->dpy, be->win, be->c_blank);

    /* remember who had the focus, so --refocus can give it back later */
    be->prev_active = active_window_before(be->dpy, be->root);
}

void nf_bm_show(struct backend *be, nf_app *a)
{
    (void)a;
    XMapRaised(be->dpy, be->win);
}

cairo_t *nf_bm_begin_frame(struct backend *be, nf_app *a)
{
    return cairo_create(cairo_xlib_surface_create(
        be->dpy, be->win, be->visual, a->w, a->h));
}

void nf_bm_end_frame(struct backend *be, cairo_t *cr)
{
    cairo_surface_t *sf = cairo_get_target(cr);
    cairo_surface_flush(sf);
    cairo_destroy(cr);
    cairo_surface_destroy(sf);
    XFlush(be->dpy);
}

void nf_bm_set_cursor(struct backend *be, int hand)
{
    Cursor c = hand ? be->c_hand : be->c_blank;
    if (be->c_current == c)
        return;
    be->c_current = c;
    XDefineCursor(be->dpy, be->win, c);
}

void nf_bm_poll(struct backend *be, nf_app *a, int timeout_ms)
{
    while (XPending(be->dpy)) {
        XEvent ev;
        XNextEvent(be->dpy, &ev);

        switch (ev.type) {
        case Expose:
            if (ev.xexpose.count == 0)
                nf_repaint(a);
            break;

        case ConfigureNotify:
            nf_resize(a, ev.xconfigure.width, ev.xconfigure.height);
            break;

        case MapNotify:
            request_focus(be);
            nf_repaint(a);
            break;

        case FocusIn:
            XGrabKeyboard(be->dpy, be->win, True, GrabModeAsync,
                          GrabModeAsync, CurrentTime);
            break;

        case MotionNotify:
            nf_motion(a, ev.xmotion.x, ev.xmotion.y);
            break;

        case ButtonPress:
            if (ev.xbutton.button == Button1)
                nf_click(a, ev.xbutton.x, ev.xbutton.y);
            break;

        case KeyPress: {
            nf_key key;
            if (translate_key(XLookupKeysym(&ev.xkey, 0), &key))
                nf_handle_key(a, key);
            break;
        }

        default:
            break;
        }
    }

    if (a->done || nf_should_stop())
        return; /* don't wait for a timeout after the answer is in */

    const int fd = ConnectionNumber(be->dpy);
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    select(fd + 1, &fds, NULL, NULL, &tv);
}

void nf_bm_restore_focus(struct backend *be, nf_app *a)
{
    if (!a->refocus || !be->prev_active || be->prev_active == be->win)
        return;

    /* hide first, so the WM has a chance to re-focus on its own as well */
    XUnmapWindow(be->dpy, be->win);
    XFlush(be->dpy);

    ask_wm_to_activate(be->dpy, be->root, be->prev_active);

    XWindowAttributes wa;
    if (XGetWindowAttributes(be->dpy, be->prev_active, &wa) &&
        wa.map_state == IsViewable)
        XSetInputFocus(be->dpy, be->prev_active, RevertToPointerRoot,
                       CurrentTime);

    XFlush(be->dpy);
}

void nf_bm_destroy(struct backend *be)
{
    XUngrabKeyboard(be->dpy, CurrentTime);
    XDestroyWindow(be->dpy, be->win);
    XFreeCursor(be->dpy, be->c_blank);
    XFreeCursor(be->dpy, be->c_hand);
    nf_bm_close(be);
}