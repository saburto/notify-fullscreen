#include <ctype.h>

#include "nf.h"

/* -------------------------------------------------------- dynamic strings */

void nf_buf_append(nf_buf *b, const char *s, size_t len)
{
    if (b->n + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 64;
        while (cap < b->n + len + 1)
            cap *= 2;
        b->s = nf_realloc(b->s, cap);
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, len);
    b->n += len;
    b->s[b->n] = '\0';
}

void nf_buf_clear(nf_buf *b)
{
    b->n = 0;
    if (b->s)
        b->s[0] = '\0';
}

void nf_buf_free(nf_buf *b)
{
    free(b->s);
    b->s = NULL;
    b->n = b->cap = 0;
}

/* ------------------------------------------------------------ line blocks */

void nf_lines_free(nf_lines *l)
{
    for (size_t i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

void nf_lines_push(nf_lines *l, const char *s, size_t len)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = nf_realloc(l->v, l->cap * sizeof *l->v);
    }
    char *copy = nf_malloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    l->v[l->n++] = copy;
}

/* --------------------------------------------------------------- measuring */

double nf_measure(cairo_t *cr, const char *s)
{
    cairo_text_extents_t te;
    cairo_text_extents(cr, s, &te);
    return te.x_advance;
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

/* Break one over-long token into chunks that fit into `maxw`. */
static void hard_break(nf_lines *out, cairo_t *cr, nf_buf *line, double maxw)
{
    while (line->n && nf_measure(cr, line->s) > maxw) {
        size_t fit = 0;
        for (size_t k = 0; k < line->n;) {
            size_t step = utf8_len((unsigned char)line->s[k]);
            if (k + step > line->n)
                step = 1;
            char saved = line->s[k + step];
            line->s[k + step] = '\0';
            double w = nf_measure(cr, line->s);
            line->s[k + step] = saved;
            if (w > maxw)
                break;
            fit = k + step;
            k += step;
        }
        if (fit == 0)
            fit = 1; /* never loop forever on a zero-width char */
        nf_lines_push(out, line->s, fit);
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

static void wrap_paragraph(nf_lines *out, cairo_t *cr, const char *p,
                           size_t len, double maxw)
{
    nf_buf line = {0};
    nf_buf cand = {0};
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

        nf_buf_clear(&cand);
        if (line.n) {
            nf_buf_append(&cand, line.s, line.n);
            nf_buf_append(&cand, " ", 1);
        }
        nf_buf_append(&cand, p + start, wlen);

        if (line.n && nf_measure(cr, cand.s) > maxw) {
            nf_lines_push(out, line.s, line.n);
            nf_buf_clear(&line);
            nf_buf_append(&line, p + start, wlen);
        } else {
            nf_buf_clear(&line);
            nf_buf_append(&line, cand.s, cand.n);
        }
        hard_break(out, cr, &line, maxw);
    }

    if (line.n || !had_word)
        nf_lines_push(out, line.n ? line.s : "", line.n);

    nf_buf_free(&line);
    nf_buf_free(&cand);
}

void nf_wrap_text(nf_lines *out, cairo_t *cr, const char *text, double maxw)
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