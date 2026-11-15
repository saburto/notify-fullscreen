/*
 * notify-fullscreen — shared core declarations.
 *
 * The core knows nothing about X11, Win32 or Cocoa: a backend (backend.h)
 * supplies the window, the event loop and a cairo surface; everything in
 * here, text.c, layout.c and preview.c is portable.
 */

#ifndef NF_H
#define NF_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>

#include "platform.h"
#include "text.h"
#include "layout.h"

struct backend;

typedef struct {
    double x, y, w, h;
} nf_rect;

/* backends translate their native keys into these actions */
typedef enum {
    NF_KEY_ACTIVATE, /* Enter / KP_Enter / Space */
    NF_KEY_CANCEL,   /* Esc */
    NF_KEY_YES,      /* y */
    NF_KEY_NO,       /* n */
    NF_KEY_QUIT,     /* q (plain mode convenience) */
    NF_KEY_TAB,      /* Tab */
    NF_KEY_LEFT,     /* Left arrow */
    NF_KEY_RIGHT     /* Right arrow */
} nf_key;

typedef struct nf_app {
    /* message and button labels (owned) */
    char *text;
    char *ok_label;
    char *no_label;
    char *tmux_pane; /* $TMUX_PANE of the invoking pane, if any */

    /* options */
    int confirm;
    int refocus;
    double opacity;
    int font_px; /* 0 = automatic */

    /* state */
    int sel;         /* 0 = none, 1 = primary button, 2 = secondary */
    int result;      /* exit status: 0 = OK/Yes, 1 = No */
    int done;
    int translucent; /* backend supports per-pixel alpha */

    int w, h; /* window size in pixels */

    nf_rect btn[2]; /* [0] = OK/Yes, [1] = No (confirm mode only) */

    nf_lines msg; /* wrapped message, rebuilt on every paint */

    struct backend *be;
} nf_app;

/* ------------------------------------------------------------------ utils */

void nf_die(const char *msg); /* runtime error, exits 3 */

void *nf_malloc(size_t n);
void *nf_realloc(void *p, size_t n);
char *nf_strdup(const char *s);
int nf_clampi(int v, int lo, int hi);

/* -------------------------------------------------------------------- app */

void nf_repaint(nf_app *a);

/* 0 = no button, 1 = primary (OK/Yes), 2 = secondary (No) */
int nf_hit_button(const nf_app *a, int x, int y);

void nf_motion(nf_app *a, int x, int y);
void nf_click(nf_app *a, int x, int y);
void nf_handle_key(nf_app *a, nf_key key);
void nf_resize(nf_app *a, int w, int h);

/* show the overlay and run the event loop until it is dismissed */
void nf_run(nf_app *a);

int nf_should_stop(void);
void nf_signal_handler(int sig);

/* ---------------------------------------------------------------- preview */

void nf_write_preview(nf_app *a, const char *path);

/* ------------------------------------------------------------------- tmux */

void nf_tmux_capture(nf_app *a);         /* remember $TMUX_PANE (with -r) */
void nf_tmux_restore(const nf_app *a);   /* re-select that pane on exit */

#endif /* NF_H */