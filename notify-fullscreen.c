/*
 * notify-fullscreen — a tiny fullscreen modal notifier for Linux (X11 / Xwayland)
 *
 * Shows a message on top of everything in a dimmed fullscreen overlay and
 * waits until the user acknowledges it with the green OK button, Enter,
 * Space or Esc.
 *
 * Build:  make
 * Usage:  notify-fullscreen "message"
 *
 * Dependencies: libX11, cairo (Xft/FreeType come with cairo).
 * License: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define VERSION "1.3.0"

#define PI 3.14159265358979323846

/* palette */
#define COL_BG_R 0.016
#define COL_BG_G 0.020
#define COL_BG_B 0.027
#define COL_TEXT_R 0.965
#define COL_TEXT_G 0.973
#define COL_TEXT_B 0.984
#define COL_OK_R 0.153
#define COL_OK_G 0.729
#define COL_OK_B 0.341
#define COL_OK_HOVER_R 0.239
#define COL_OK_HOVER_G 0.843
#define COL_OK_HOVER_B 0.451
#define COL_NO_R 0.169
#define COL_NO_G 0.196
#define COL_NO_B 0.231
#define COL_NO_HOVER_R 0.239
#define COL_NO_HOVER_G 0.275
#define COL_NO_HOVER_B 0.325

/* ------------------------------------------------------------------ utils */

static void die(const char *msg)
{
    fprintf(stderr, "notify-fullscreen: %s\n", msg);
    exit(3); /* runtime error; 1 is reserved for "answered No" */
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        die("out of memory");
    return q;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = xmalloc(n);
    memcpy(c, s, n);
    return c;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* set from a signal handler, polled by the event loop */
static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

static size_t utf8_len(unsigned char c)
{
    if (c < 0x80)
        return 1;
    if ((c & 0xE0) == 0xC0)
        return 2;
    if ((c & 0xF0) == 0xE0)
        return 3;
    if ((c & 0xF8) == 0xF0)
        return 4;
    return 1; /* invalid byte: advance one */
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                         double r)
{
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, PI / 2, PI);
    cairo_arc(cr, x + r, y + r, r, PI, 3 * PI / 2);
    cairo_close_path(cr);
}

/* -------------------------------------------------------- dynamic strings */

typedef struct {
    char *s;
    size_t n, cap;
} sbuf;

static void sb_append(sbuf *b, const char *s, size_t len)
{
    if (b->n + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->n + len + 1)
            cap *= 2;
        b->s = xrealloc(b->s, cap);
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, len);
    b->n += len;
    b->s[b->n] = '\0';
}

static void sb_clear(sbuf *b)
{
    b->n = 0;
    if (b->s)
        b->s[0] = '\0';
}

static void sb_free(sbuf *b)
{
    free(b->s);
    b->s = NULL;
    b->n = b->cap = 0;
}

/* ------------------------------------------------------------ line blocks */

typedef struct {
    char **v;
    size_t n, cap;
} lines_t;

static void lines_free(lines_t *l)
{
    for (size_t i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

static void lines_push(lines_t *l, const char *s, size_t len)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = xrealloc(l->v, l->cap * sizeof *l->v);
    }
    char *copy = xmalloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    l->v[l->n++] = copy;
}

static double measure(cairo_t *cr, const char *s)
{
    cairo_text_extents_t te;
    cairo_text_extents(cr, s, &te);
    return te.x_advance;
}

/* Break one over-long token into chunks that fit into `maxw`. */
static void hard_break(lines_t *out, cairo_t *cr, sbuf *line, double maxw)
{
    while (line->n && measure(cr, line->s) > maxw) {
        size_t fit = 0;
        for (size_t k = 0; k < line->n;) {
            size_t step = utf8_len((unsigned char)line->s[k]);
            if (k + step > line->n)
                step = 1;
            char saved = line->s[k + step];
            line->s[k + step] = '\0';
            double w = measure(cr, line->s);
            line->s[k + step] = saved;
            if (w > maxw)
                break;
            fit = k + step;
            k += step;
        }
        if (fit == 0)
            fit = 1; /* never loop forever on a zero-width char */
        lines_push(out, line->s, fit);
        memmove(line->s, line->s + fit, line->n - fit);
        line->n -= fit;
        line->s[line->n] = '\0';
        while (line->n && line->s[0] == ' ') {
            memmove(line->s, line->s + 1, line->n - 1);
            line->n--;
            line->s[line->n] = '\0';
        }
    }
}

static void wrap_paragraph(lines_t *out, cairo_t *cr, const char *p, size_t len,
                           double maxw)
{
    sbuf line = {0};
    sbuf cand = {0};
    size_t i = 0;
    int had_word = 0;

    while (i < len) {
        while (i < len && isspace((unsigned char)p[i]))
            i++;
        if (i >= len)
            break;
        size_t start = i;
        while (i < len && !isspace((unsigned char)p[i]))
            i++;
        size_t wlen = i - start;
        had_word = 1;

        sb_clear(&cand);
        if (line.n) {
            sb_append(&cand, line.s, line.n);
            sb_append(&cand, " ", 1);
        }
        sb_append(&cand, p + start, wlen);

        if (line.n && measure(cr, cand.s) > maxw) {
            lines_push(out, line.s, line.n);
            sb_clear(&line);
            sb_append(&line, p + start, wlen);
        } else {
            sb_clear(&line);
            sb_append(&line, cand.s, cand.n);
        }
        hard_break(out, cr, &line, maxw);
    }

    if (line.n || !had_word)
        lines_push(out, line.n ? line.s : "", line.n);

    sb_free(&line);
    sb_free(&cand);
}

static void wrap_text(lines_t *out, cairo_t *cr, const char *text, double maxw)
{
    const char *p = text;
    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        wrap_paragraph(out, cr, p, len, maxw);
        if (!nl)
            break;
        p = nl + 1;
    }
}

/* --------------------------------------------------------------- the app */

typedef struct {
    char *text;     /* raw message (owned) */
    char *ok_label; /* primary button label (owned) */
    char *no_label; /* secondary button label (owned) */

    Display *dpy;
    Window root;
    Window win;
    Visual *visual;
    int argb; /* window has an alpha channel => real transparency */
    Colormap cmap;

    int w, h;

    double opacity; /* 0..1 background alpha */
    int font_px;    /* 0 = automatic */

    lines_t msg;

    /* buttons: [0] = Yes/OK, [1] = No (--confirm only) */
    int confirm;
    int sel; /* 0 = none/hover away, 1 = first button, 2 = second */
    int refocus;      /* restore focus to the triggering window */
    Window prev_active; /* window that was active before we mapped */
    char *tmux_pane;    /* $TMUX_PANE of the invoking pane (owned) */
    double btn_x, btn_y, btn_w, btn_h;
    double btn2_x, btn2_y, btn2_w, btn2_h;

    int result; /* exit status: 0 = yes / acknowledged, 1 = no */

    Cursor c_blank, c_hand, c_current;

    int done;
} app_t;

static int wrap_at(app_t *a, cairo_t *cr, int fs, double maxw)
{
    cairo_set_font_size(cr, fs);
    lines_free(&a->msg);
    wrap_text(&a->msg, cr, a->text, maxw);
    return fs;
}

/*
 * Wrap the message and pick a font size: short messages get a bigger font,
 * very long ones are shrunk until they fit above the button.
 * Leaves the message wrapped and `cr`'s font size set to the result.
 */
static int fit_message(app_t *a, cairo_t *cr, int fs, double maxw, double max_h)
{
    const int explicit_size = a->font_px > 0;
    const int cap = clampi((int)(a->h * 0.09), fs, 240);
    const double lh_ratio = 1.34;

    if (max_h < 40)
        max_h = a->h * 0.3;

    wrap_at(a, cr, fs, maxw);

    if (!explicit_size && strlen(a->text) <= 140) {
        for (int i = 0; i < 4; i++) {
            double widest = 0;
            for (size_t k = 0; k < a->msg.n; k++) {
                double w = measure(cr, a->msg.v[k]);
                if (w > widest)
                    widest = w;
            }
            if (widest <= 0)
                break;
            const double want = a->w * 0.55;
            if (widest >= want * 0.92)
                break;
            int next = (int)(fs * (want / widest));
            if (next > cap)
                next = cap;
            if (next <= fs)
                break;
            fs = next;
            wrap_at(a, cr, fs, maxw);
        }
    }

    while (fs > 9 && a->msg.n * (fs * lh_ratio) > max_h) {
        fs = (int)(fs * 0.9);
        wrap_at(a, cr, fs, maxw);
    }

    cairo_set_font_size(cr, fs);
    return fs;
}

static void draw_label(cairo_t *cr, const char *label, int font_px, double bx,
                       double by, double bw, double bh)
{
    cairo_set_font_size(cr, font_px);
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, bx + (bw - te.x_advance) / 2.0 - te.x_bearing,
                  by + bh / 2.0 - te.y_bearing - te.height / 2.0);
    cairo_show_text(cr, label);
}

static void layout_and_paint(app_t *a, cairo_t *cr)
{
    const int W = a->w, H = a->h;

    /* dimmed (or opaque, without a compositor) background */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, COL_BG_R, COL_BG_G, COL_BG_B,
                          a->argb ? a->opacity : 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    /* message */
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);

    int fs = a->font_px > 0 ? a->font_px
                            : clampi((int)(H * 0.052), 16, 160);
    if (a->font_px <= 0 && W < 900)
        fs = clampi((int)(fs * 0.8), 14, 160);
    fs = fit_message(a, cr, fs, W * 0.80, H * 0.62);

    /* the buttons are sized from the message font; shrink both until it fits */
    int bf = 0;
    double yes_w = 0, no_w = 0, row_w = 0, bgap = 0, btn_h = 0, gap = 0,
           lh = 0, msg_h = 0, total = 0;
    for (int iter = 0;; iter++) {
        bf = clampi((int)(fs * 0.45), 14, 72);
        cairo_set_font_size(cr, bf);

        btn_h = bf * 2.4;
        const double pad = btn_h * 1.9;
        const double min_w = btn_h * 2.8;

        yes_w = measure(cr, a->ok_label) + pad;
        if (yes_w < min_w)
            yes_w = min_w;

        if (a->confirm) {
            no_w = measure(cr, a->no_label) + pad;
            if (no_w < min_w)
                no_w = min_w;
            if (no_w > yes_w)
                yes_w = no_w;
            no_w = yes_w;
            bgap = btn_h * 0.35;
            const double avail = W * 0.82 - bgap;
            if (yes_w > avail / 2)
                yes_w = no_w = avail / 2;
            row_w = yes_w * 2 + bgap;
        } else {
            if (yes_w > W * 0.55)
                yes_w = W * 0.55;
            row_w = yes_w;
        }

        gap = btn_h * 0.85;
        lh = fs * 1.34;
        msg_h = a->msg.n * lh;
        total = msg_h + gap + btn_h;

        if (total <= H * 0.90 || iter > 12 || fs <= 9)
            break;

        fs = (int)(fs * 0.93);
        wrap_at(a, cr, fs, W * 0.80);
    }

    double top = (H - total) / 2.0;
    if (top < H * 0.05)
        top = H * 0.05;

    /* message lines, centered one by one */
    cairo_set_font_size(cr, fs);
    cairo_set_source_rgb(cr, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
    double y = top;
    for (size_t i = 0; i < a->msg.n; i++) {
        cairo_text_extents_t te;
        cairo_text_extents(cr, a->msg.v[i], &te);
        cairo_move_to(cr, (W - te.x_advance) / 2.0 - te.x_bearing,
                      y + lh / 2.0 - te.y_bearing - te.height / 2.0);
        cairo_show_text(cr, a->msg.v[i]);
        y += lh;
    }

    /* buttons */
    const double radius = btn_h * 0.26;
    const double row_x = (W - row_w) / 2.0;
    const double by = top + msg_h + gap;

    a->btn_x = row_x;
    a->btn_y = by;
    a->btn_w = yes_w;
    a->btn_h = btn_h;
    a->btn2_x = row_x + yes_w + bgap;
    a->btn2_y = by;
    a->btn2_w = no_w;
    a->btn2_h = btn_h;

    /* primary button: green (Yes / OK) */
    if (a->sel == 1)
        cairo_set_source_rgb(cr, COL_OK_HOVER_R, COL_OK_HOVER_G,
                             COL_OK_HOVER_B);
    else
        cairo_set_source_rgb(cr, COL_OK_R, COL_OK_G, COL_OK_B);
    rounded_rect(cr, a->btn_x, by, yes_w, btn_h, radius);
    cairo_fill(cr);
    draw_label(cr, a->ok_label, bf, a->btn_x, by, yes_w, btn_h);

    if (a->confirm) {
        /* secondary button: neutral */
        if (a->sel == 2)
            cairo_set_source_rgb(cr, COL_NO_HOVER_R, COL_NO_HOVER_G,
                                 COL_NO_HOVER_B);
        else
            cairo_set_source_rgb(cr, COL_NO_R, COL_NO_G, COL_NO_B);
        rounded_rect(cr, a->btn2_x, by, no_w, btn_h, radius);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        draw_label(cr, a->no_label, bf, a->btn2_x, by, no_w, btn_h);

        /* focus ring around the selected button */
        const double rx = a->sel == 2 ? a->btn2_x : a->btn_x;
        const double rw = a->sel == 2 ? no_w : yes_w;
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
        cairo_set_line_width(cr, 2.0);
        rounded_rect(cr, rx - 4, by - 4, rw + 8, btn_h + 8, radius + 4);
        cairo_stroke(cr);
    }
}

static void repaint(app_t *a)
{
    cairo_surface_t *sf =
        cairo_xlib_surface_create(a->dpy, a->win, a->visual, a->w, a->h);
    cairo_t *cr = cairo_create(sf);
    layout_and_paint(a, cr);
    cairo_surface_flush(sf);
    cairo_destroy(cr);
    cairo_surface_destroy(sf);
    XFlush(a->dpy);
}

static int inside(double x, double y, double bx, double by, double bw,
                  double bh)
{
    return x >= bx && x <= bx + bw && y >= by && y <= by + bh;
}

/* 0 = no button, 1 = primary (Yes/OK), 2 = secondary (No) */
static int hit_button(const app_t *a, int x, int y)
{
    if (inside(x, y, a->btn_x, a->btn_y, a->btn_w, a->btn_h))
        return 1;
    if (a->confirm &&
        inside(x, y, a->btn2_x, a->btn2_y, a->btn2_w, a->btn2_h))
        return 2;
    return 0;
}

static void set_cursor(app_t *a, Cursor c)
{
    if (a->c_current == c)
        return;
    a->c_current = c;
    XDefineCursor(a->dpy, a->win, c);
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

/* X errors are not fatal here: focus/grab races happen under Xwayland. */
static int on_x_error(Display *dpy, XErrorEvent *ev)
{
    (void)dpy;
    (void)ev;
    return 0;
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
        /* no EWMH-ish state, try the plain X input focus */
        int revert = 0;
        Window f = None;
        XGetInputFocus(dpy, &f, &revert);
        if (f != None && f != PointerRoot && f != root)
            w = f;
    }
    return w;
}

/* Ask the window manager to give us the keyboard focus. */
static void request_focus(app_t *a)
{
    ask_wm_to_activate(a->dpy, a->root, a->win);

    XWindowAttributes wa;
    if (!XGetWindowAttributes(a->dpy, a->win, &wa) ||
        wa.map_state != IsViewable)
        return; /* XSetInputFocus/XGrabKeyboard would raise BadMatch */

    XSetInputFocus(a->dpy, a->win, RevertToPointerRoot, CurrentTime);
    XGrabKeyboard(a->dpy, a->win, True, GrabModeAsync, GrabModeAsync,
                  CurrentTime);
}

/* Give the keyboard focus back to the window that triggered us (--refocus). */
static void restore_previous_focus(app_t *a)
{
    if (!a->refocus || !a->prev_active || a->prev_active == a->win)
        return;

    /* hide first, so the WM has a chance to re-focus on its own as well */
    XUnmapWindow(a->dpy, a->win);
    XFlush(a->dpy);

    ask_wm_to_activate(a->dpy, a->root, a->prev_active);

    XWindowAttributes wa;
    if (XGetWindowAttributes(a->dpy, a->prev_active, &wa) &&
        wa.map_state == IsViewable)
        XSetInputFocus(a->dpy, a->prev_active, RevertToPointerRoot,
                       CurrentTime);

    XFlush(a->dpy);
}

/* A tmux pane id looks like "%12". */
static int valid_pane_id(const char *s)
{
    if (!s || s[0] != '%' || !s[1])
        return 0;
    for (const char *p = s + 1; *p; p++)
        if (!isdigit((unsigned char)*p))
            return 0;
    return 1;
}

/*
 * If we were started from a tmux pane, re-select that pane and its window
 * after the notification, so typing continues where the command came from.
 */
static void focus_tmux_pane(const app_t *a)
{
    if (!a->refocus || !a->tmux_pane)
        return;

    pid_t pid = fork();
    if (pid != 0)
        return; /* parent (or fork failed): nothing to wait for */

    execlp("tmux", "tmux",
           "select-window", "-t", a->tmux_pane,
           ";",
           "select-pane", "-t", a->tmux_pane,
           (char *)NULL);
    _exit(127); /* no tmux client on PATH */
}

/* ---------------------------------------------------------------- window */

static void set_atoms(app_t *a)
{
    Atom state = XInternAtom(a->dpy, "_NET_WM_STATE", False);
    Atom vals[3] = {
        XInternAtom(a->dpy, "_NET_WM_STATE_FULLSCREEN", False),
        XInternAtom(a->dpy, "_NET_WM_STATE_ABOVE", False),
        XInternAtom(a->dpy, "_NET_WM_STATE_SKIP_TASKBAR", False),
    };
    XChangeProperty(a->dpy, a->win, state, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)vals, 3);

    Atom type = XInternAtom(a->dpy, "_NET_WM_WINDOW_TYPE", False);
    Atom normal = XInternAtom(a->dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    XChangeProperty(a->dpy, a->win, type, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&normal, 1);

    /* keep the notification out of the WM's minimise/close UI */
    Atom pid = XInternAtom(a->dpy, "_NET_WM_PID", False);
    long pidval = (long)getpid();
    XChangeProperty(a->dpy, a->win, pid, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&pidval, 1);

    XStoreName(a->dpy, a->win, "notify-fullscreen");
    XClassHint hint = {(char *)"notify-fullscreen",
                       (char *)"Notify-fullscreen"};
    XSetClassHint(a->dpy, a->win, &hint);
}

/* --------------------------------------------------------- preview (png) */

static void write_preview(app_t *a, const char *path)
{
    cairo_surface_t *sf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, a->w, a->h);
    cairo_t *cr = cairo_create(sf);
    layout_and_paint(a, cr);
    cairo_destroy(cr);
    cairo_status_t st = cairo_surface_write_to_png(sf, path);
    cairo_surface_destroy(sf);
    if (st != CAIRO_STATUS_SUCCESS)
        die(cairo_status_to_string(st));
}

/* ------------------------------------------------------------------ main */

static void usage(FILE *out)
{
    fprintf(out,
            "usage: notify-fullscreen [options] MESSAGE\n"
            "\n"
            "Show MESSAGE in a fullscreen, dimmed overlay until it is\n"
            "acknowledged with the green OK button, Enter, Space or Esc.\n"
            "\n"
            "options:\n"
            "  -c, --confirm           ask a yes/no question: green Yes exits 0,\n"
            "                          No (or Esc / n) exits 1\n"
            "  -r, --refocus           give keyboard focus back to the window and\n"
            "                          tmux pane that triggered the notification\n"
            "  -o, --opacity FLOAT     background opacity 0.0-1.0 "
            "(default 0.85)\n"
            "  -f, --font-size PX      message font size in pixels "
            "(default: screen based)\n"
            "      --ok-label TEXT     primary button label "
            "(default \"OK\", \"Yes\" with -c)\n"
            "      --no-label TEXT     secondary button label "
            "(default \"No\", only with -c)\n"
            "  -h, --help              show this help and exit\n"
            "  -V, --version           print version and exit\n"
            "\n"
            "MESSAGE may span several arguments (joined with spaces);\n"
            "use \"-\" to read the message from standard input.\n"
            "\n"
            "exit status: 0 = OK/Yes, 1 = No, 2 = usage error,\n"
            "             3 = runtime error, 130 = interrupted\n");
}

static char *read_stdin(void)
{
    size_t cap = 4096, n = 0;
    char *buf = xmalloc(cap);

    for (;;) {
        if (n + 1 >= cap)
            buf = xrealloc(buf, cap *= 2);
        size_t got = fread(buf + n, 1, cap - n - 1, stdin);
        if (got == 0)
            break;
        n += got;
    }
    buf[n] = '\0';

    /* drop trailing newlines, like pipelines from `echo` produce */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';

    if (n == 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

static char *join_args(int argc, char **argv, int first)
{
    size_t len = 0;
    for (int i = first; i < argc; i++)
        len += strlen(argv[i]) + 1;
    char *buf = xmalloc(len + 1);
    buf[0] = '\0';
    for (int i = first; i < argc; i++) {
        if (i > first)
            strcat(buf, " ");
        strcat(buf, argv[i]);
    }
    return buf;
}

int main(int argc, char **argv)
{
    app_t a;
    memset(&a, 0, sizeof a);
    a.opacity = 0.85;
    a.font_px = 0;

    const char *preview_path = NULL;
    int preview_w = 0, preview_h = 0;
    int first_msg = argc;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
            printf("notify-fullscreen %s\n", VERSION);
            return 0;
        }
        if (!strcmp(arg, "-o") || !strcmp(arg, "--opacity")) {
            if (++i >= argc) {
                fprintf(stderr, "notify-fullscreen: --opacity needs a value\n");
                return 2;
            }
            char *end = NULL;
            double v = strtod(argv[i], &end);
            if (!end || *end || !(v >= 0.0 && v <= 1.0)) {
                fprintf(stderr, "notify-fullscreen: opacity must be 0.0-1.0\n");
                return 2;
            }
            a.opacity = v;
            continue;
        }
        if (!strcmp(arg, "-f") || !strcmp(arg, "--font-size")) {
            if (++i >= argc) {
                fprintf(stderr,
                        "notify-fullscreen: --font-size needs a value\n");
                return 2;
            }
            char *end = NULL;
            long v = strtol(argv[i], &end, 10);
            if (!end || *end || v < 6 || v > 400) {
                fprintf(stderr,
                        "notify-fullscreen: font size must be 6-400 px\n");
                return 2;
            }
            a.font_px = (int)v;
            continue;
        }
        if (!strcmp(arg, "-c") || !strcmp(arg, "--confirm")) {
            a.confirm = 1;
            continue;
        }
        if (!strcmp(arg, "-r") || !strcmp(arg, "--refocus")) {
            a.refocus = 1;
            continue;
        }
        if (!strcmp(arg, "--no-label")) {
            if (++i >= argc) {
                fprintf(stderr, "notify-fullscreen: --no-label needs a value\n");
                return 2;
            }
            free(a.no_label);
            a.no_label = xstrdup(argv[i]);
            continue;
        }
        if (!strcmp(arg, "--ok-label")) {
            if (++i >= argc) {
                fprintf(stderr, "notify-fullscreen: --ok-label needs a value\n");
                return 2;
            }
            free(a.ok_label);
            a.ok_label = xstrdup(argv[i]);
            continue;
        }
        if (!strcmp(arg, "--preview-png")) {
            if (++i >= argc) {
                fprintf(stderr,
                        "notify-fullscreen: --preview-png needs a path\n");
                return 2;
            }
            preview_path = argv[i];
            continue;
        }
        if (!strcmp(arg, "--preview-size")) {
            if (++i >= argc ||
                sscanf(argv[i], "%dx%d", &preview_w, &preview_h) != 2 ||
                preview_w < 200 || preview_h < 200) {
                fprintf(stderr,
                        "notify-fullscreen: --preview-size needs WxH\n");
                return 2;
            }
            continue;
        }
        if (!strcmp(arg, "--")) {
            first_msg = i + 1;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0' && strcmp(arg, "-")) {
            fprintf(stderr, "notify-fullscreen: unknown option %s\n", arg);
            return 2;
        }
        first_msg = i;
        break;
    }

    if (!a.ok_label)
        a.ok_label = xstrdup(a.confirm ? "Yes" : "OK");
    if (!a.no_label)
        a.no_label = xstrdup("No");
    a.sel = a.confirm ? 1 : 0; /* Yes is preselected in yes/no mode */

    /* remember our tmux pane (only relevant together with --refocus) */
    if (a.refocus) {
        const char *pane = getenv("TMUX_PANE");
        if (valid_pane_id(pane))
            a.tmux_pane = xstrdup(pane);
    }

    if (first_msg < argc) {
        if (!strcmp(argv[first_msg], "-")) {
            a.text = read_stdin();
            if (!a.text) {
                fprintf(stderr, "notify-fullscreen: empty message on stdin\n");
                return 2;
            }
        } else {
            a.text = join_args(argc, argv, first_msg);
        }
    } else if (!isatty(STDIN_FILENO)) {
        a.text = read_stdin();
        if (!a.text) {
            usage(stderr);
            return 2;
        }
    } else {
        usage(stderr);
        return 2;
    }

    /* ---- display ---- */
    if (preview_path && preview_w > 0 && preview_h > 0) {
        /* headless preview: no DISPLAY needed, fonts come from fontconfig */
        a.argb = 1;
        a.w = preview_w;
        a.h = preview_h;
        write_preview(&a, preview_path);
        lines_free(&a.msg);
        free(a.text);
        free(a.ok_label);
        free(a.no_label);
        free(a.tmux_pane);
        printf("wrote %s (%dx%d)\n", preview_path, a.w, a.h);
        return 0;
    }

    a.dpy = XOpenDisplay(NULL);
    if (!a.dpy) {
        die("cannot open X display (is DISPLAY set? "
            "is Xwayland/X11 running?)");
    }
    a.root = DefaultRootWindow(a.dpy);
    XSetErrorHandler(on_x_error);
    int screen = DefaultScreen(a.dpy);
    a.w = preview_w > 0 ? preview_w : DisplayWidth(a.dpy, screen);
    a.h = preview_h > 0 ? preview_h : DisplayHeight(a.dpy, screen);

    if (preview_path) {
        a.argb = 1; /* keep the alpha channel so the PNG shows the dimming */
        write_preview(&a, preview_path);
        lines_free(&a.msg);
        free(a.text);
        free(a.ok_label);
        free(a.no_label);
        free(a.tmux_pane);
        XCloseDisplay(a.dpy);
        printf("wrote %s (%dx%d)\n", preview_path, a.w, a.h);
        return 0;
    }

    /* pick a 32-bit ARGB visual so the background can be translucent */
    XVisualInfo vinfo;
    if (XMatchVisualInfo(a.dpy, screen, 32, TrueColor, &vinfo)) {
        a.visual = vinfo.visual;
        a.argb = 1;
    } else {
        a.visual = DefaultVisual(a.dpy, screen);
        a.argb = 0;
    }

    XSetWindowAttributes at;
    memset(&at, 0, sizeof at);
    at.border_pixel = 0;
    at.background_pixel = 0;
    at.background_pixmap = None;
    at.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                    PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    if (a.argb) {
        a.cmap = XCreateColormap(a.dpy, a.root, a.visual, AllocNone);
        at.colormap = a.cmap;
    }
    a.win = XCreateWindow(a.dpy, a.root, 0, 0, (unsigned)a.w, (unsigned)a.h, 0,
                          a.argb ? 32 : DefaultDepth(a.dpy, screen),
                          InputOutput, a.visual,
                          CWColormap | CWBorderPixel | CWBackPixel |
                              CWBackPixmap | CWEventMask,
                          &at);
    set_atoms(&a);

    a.c_blank = make_blank_cursor(a.dpy);
    a.c_hand = XCreateFontCursor(a.dpy, XC_hand2);
    a.c_current = a.c_blank;
    XDefineCursor(a.dpy, a.win, a.c_blank);

    /* remember who had the focus, so --refocus can give it back later */
    a.prev_active = active_window_before(a.dpy, a.root);

    XMapRaised(a.dpy, a.win);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    int xfd = ConnectionNumber(a.dpy);
    fd_set fds;

    while (!a.done) {
        while (XPending(a.dpy)) {
            XEvent ev;
            XNextEvent(a.dpy, &ev);
            switch (ev.type) {
            case Expose:
                if (ev.xexpose.count == 0)
                    repaint(&a);
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != a.w ||
                    ev.xconfigure.height != a.h) {
                    a.w = ev.xconfigure.width;
                    a.h = ev.xconfigure.height;
                    repaint(&a);
                }
                break;
            case MapNotify:
                request_focus(&a);
                repaint(&a);
                break;
            case FocusIn:
                XGrabKeyboard(a.dpy, a.win, True, GrabModeAsync,
                              GrabModeAsync, CurrentTime);
                break;
            case MotionNotify: {
                const int hit = hit_button(&a, ev.xmotion.x, ev.xmotion.y);
                int newsel = hit;
                if (a.confirm && hit == 0)
                    newsel = a.sel ? a.sel : 1; /* keep keyboard selection */
                set_cursor(&a, hit ? a.c_hand : a.c_blank);
                if (newsel != a.sel) {
                    a.sel = newsel;
                    repaint(&a);
                }
                break;
            }
            case ButtonPress: {
                if (ev.xbutton.button != Button1)
                    break;
                const int hit = hit_button(&a, ev.xbutton.x, ev.xbutton.y);
                if (hit == 1) {
                    a.result = 0;
                    a.done = 1;
                } else if (hit == 2) {
                    a.result = 1;
                    a.done = 1;
                }
                break;
            }
            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (!a.confirm) {
                    if (ks == XK_Return || ks == XK_KP_Enter ||
                        ks == XK_space || ks == XK_Escape || ks == XK_q ||
                        ks == XK_y) {
                        a.result = 0;
                        a.done = 1;
                    }
                    break;
                }
                if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_space) {
                    a.result = a.sel == 2 ? 1 : 0;
                    a.done = 1;
                } else if (ks == XK_y || ks == XK_Y) {
                    a.result = 0;
                    a.done = 1;
                } else if (ks == XK_n || ks == XK_N || ks == XK_Escape) {
                    a.result = 1;
                    a.done = 1;
                } else if (ks == XK_Tab) {
                    a.sel = a.sel == 2 ? 1 : 2;
                    repaint(&a);
                } else if (ks == XK_Left && a.sel != 1) {
                    a.sel = 1;
                    repaint(&a);
                } else if (ks == XK_Right && a.sel != 2) {
                    a.sel = 2;
                    repaint(&a);
                }
                break;
            }
            default:
                break;
            }
        }
        if (a.done || g_stop)
            break;
        FD_ZERO(&fds);
        FD_SET(xfd, &fds);
        struct timeval tv = {0, 200000};
        select(xfd + 1, &fds, NULL, NULL, &tv);
    }

    XUngrabKeyboard(a.dpy, CurrentTime);
    restore_previous_focus(&a);
    focus_tmux_pane(&a);
    XDestroyWindow(a.dpy, a.win);
    XFreeCursor(a.dpy, a.c_blank);
    XFreeCursor(a.dpy, a.c_hand);
    lines_free(&a.msg);
    free(a.text);
    free(a.ok_label);
    free(a.no_label);
    free(a.tmux_pane);
    XCloseDisplay(a.dpy);
    return g_stop ? 130 : a.result;
}