#include "nf.h"

void nf_die(const char *msg)
{
    fprintf(stderr, "notify-fullscreen: %s\n", msg);
    exit(3); /* runtime error; 1 is reserved for "answered No" */
}

void *nf_malloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        nf_die("out of memory");
    return p;
}

void *nf_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        nf_die("out of memory");
    return q;
}

char *nf_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = nf_malloc(n);
    memcpy(c, s, n);
    return c;
}

int nf_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}