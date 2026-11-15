#ifndef NF_LAYOUT_H
#define NF_LAYOUT_H

#include <cairo/cairo.h>

struct nf_app;

/*
 * Lay out the message and the buttons for the current window size and paint
 * everything into `cr`. Updates app->btn[] so hit-testing matches what is on
 * screen.
 */
void nf_paint(struct nf_app *a, cairo_t *cr);

#endif /* NF_LAYOUT_H */