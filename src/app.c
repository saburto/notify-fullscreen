/*
 * Core interaction logic: repaint bridge, hit testing, input handling and
 * the main loop. Everything here is platform independent.
 */

#include <signal.h>

#include "backend.h"
#include "nf.h"

static volatile sig_atomic_t g_stop;

int nf_should_stop(void)
{
    return g_stop != 0;
}

void nf_signal_handler(int sig)
{
    (void)sig;
    g_stop = 1;
}

void nf_repaint(nf_app *a)
{
    cairo_t *cr = nf_bm_begin_frame(a->be, a);
    nf_paint(a, cr);
    nf_bm_end_frame(a->be, cr);
}

static int inside(double x, double y, const nf_rect *r)
{
    return x >= r->x && x <= r->x + r->w && y >= r->y && y <= r->y + r->h;
}

int nf_hit_button(const nf_app *a, int x, int y)
{
    if (inside(x, y, &a->btn[0]))
        return 1;
    if (a->confirm && inside(x, y, &a->btn[1]))
        return 2;
    return 0;
}

void nf_motion(nf_app *a, int x, int y)
{
    const int hit = nf_hit_button(a, x, y);
    int newsel = hit;

    if (a->confirm && hit == 0)
        newsel = a->sel ? a->sel : 1; /* keep keyboard selection */

    nf_bm_set_cursor(a->be, hit != 0);

    if (newsel != a->sel) {
        a->sel = newsel;
        nf_repaint(a);
    }
}

void nf_click(nf_app *a, int x, int y)
{
    const int hit = nf_hit_button(a, x, y);

    if (hit == 1) {
        a->result = 0;
        a->done = 1;
    } else if (hit == 2) {
        a->result = 1;
        a->done = 1;
    }
}

void nf_handle_key(nf_app *a, nf_key key)
{
    if (!a->confirm) {
        if (key == NF_KEY_ACTIVATE || key == NF_KEY_CANCEL ||
            key == NF_KEY_YES || key == NF_KEY_QUIT) {
            a->result = 0;
            a->done = 1;
        }
        return;
    }

    switch (key) {
    case NF_KEY_ACTIVATE:
        a->result = a->sel == 2 ? 1 : 0;
        a->done = 1;
        break;
    case NF_KEY_YES:
        a->result = 0;
        a->done = 1;
        break;
    case NF_KEY_NO:
    case NF_KEY_CANCEL:
        a->result = 1;
        a->done = 1;
        break;
    case NF_KEY_TAB:
        a->sel = a->sel == 2 ? 1 : 2;
        nf_repaint(a);
        break;
    case NF_KEY_LEFT:
        if (a->sel != 1) {
            a->sel = 1;
            nf_repaint(a);
        }
        break;
    case NF_KEY_RIGHT:
        if (a->sel != 2) {
            a->sel = 2;
            nf_repaint(a);
        }
        break;
    default:
        break;
    }
}

void nf_resize(nf_app *a, int w, int h)
{
    if (w == a->w && h == a->h)
        return;
    a->w = w;
    a->h = h;
    nf_repaint(a);
}

void nf_run(nf_app *a)
{
    nf_bm_show(a->be, a);

    while (!a->done && !nf_should_stop())
        nf_bm_poll(a->be, a, 200);
}