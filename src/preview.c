/*
 * Render the overlay to a PNG instead of showing it. Portable: pure cairo
 * image surface, so it also works headless (given an explicit size).
 */

#include "nf.h"

void nf_write_preview(nf_app *a, const char *path)
{
    cairo_surface_t *sf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, a->w, a->h);
    cairo_t *cr = cairo_create(sf);

    nf_paint(a, cr);

    cairo_destroy(cr);
    cairo_status_t st = cairo_surface_write_to_png(sf, path);
    cairo_surface_destroy(sf);

    if (st != CAIRO_STATUS_SUCCESS)
        nf_die(cairo_status_to_string(st));
}