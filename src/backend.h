/*
 * Backend interface.
 *
 * A backend provides, for one platform: a fullscreen always-on-top window,
 * an event loop that reports semantic keys/mouse actions to the core, and a
 * cairo surface to paint into. Implementations live in src/backend/.
 */

#ifndef NF_BACKEND_H
#define NF_BACKEND_H

#include "nf.h"

struct backend;

struct backend *nf_backend_new(void);

/* connect to the display server; dies with a message (exit 3) on failure */
void nf_bm_open(struct backend *be);
void nf_bm_close(struct backend *be);

/* usable screen size in pixels (primary monitor) */
void nf_bm_screen_size(struct backend *be, int *w, int *h);

/* create the window using app->w/height; sets app->translucent */
void nf_bm_create_window(struct backend *be, nf_app *a);

/* map/raise the window (the backend requests keyboard focus via its events) */
void nf_bm_show(struct backend *be, nf_app *a);

/* dispatch pending events, then wait up to timeout_ms for more */
void nf_bm_poll(struct backend *be, nf_app *a, int timeout_ms);

cairo_t *nf_bm_begin_frame(struct backend *be, nf_app *a);
void nf_bm_end_frame(struct backend *be, cairo_t *cr);

void nf_bm_set_cursor(struct backend *be, int hand);

/* --refocus: activate the window that was active before we appeared */
void nf_bm_restore_focus(struct backend *be, nf_app *a);

/* release window/cursors and close the display (does not free app) */
void nf_bm_destroy(struct backend *be);

#endif /* NF_BACKEND_H */