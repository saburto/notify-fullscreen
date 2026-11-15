/*
 * Text measuring, UTF-8 helpers and word wrapping.
 * Pure cairo + libc, no platform dependencies.
 */

#ifndef NF_TEXT_H
#define NF_TEXT_H

#include <stddef.h>
#include <cairo/cairo.h>

/* growable UTF-8 string buffer, used while wrapping */
typedef struct {
    char *s;
    size_t n, cap;
} nf_buf;

/* wrapped message lines (owned, NUL terminated strings) */
typedef struct {
    char **v;
    size_t n, cap;
} nf_lines;

void nf_buf_append(nf_buf *b, const char *s, size_t len);
void nf_buf_clear(nf_buf *b);
void nf_buf_free(nf_buf *b);

void nf_lines_free(nf_lines *l);
void nf_lines_push(nf_lines *l, const char *s, size_t len);

/* advance width of `s` with the cairo context's current font */
double nf_measure(cairo_t *cr, const char *s);

/*
 * Wrap `text` into `out` at `maxw` pixels, honouring explicit newlines
 * (an empty line stays empty) and hard-breaking words that are too long.
 */
void nf_wrap_text(nf_lines *out, cairo_t *cr, const char *text, double maxw);

#endif /* NF_TEXT_H */