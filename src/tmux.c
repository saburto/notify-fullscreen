/*
 * tmux pane restore (POSIX): remember $TMUX_PANE and re-select that pane and
 * its window once the notification is dismissed.
 *
 * Windows builds run under WSL (the Linux binary) or have no tmux, so this is
 * compiled to a no-op there.
 */

#include <ctype.h>
#include <unistd.h>

#include "nf.h"

#ifdef _WIN32

void nf_tmux_capture(nf_app *a)
{
    (void)a;
}

void nf_tmux_restore(const nf_app *a)
{
    (void)a;
}

#else

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

void nf_tmux_capture(nf_app *a)
{
    if (!a->refocus)
        return;

    const char *pane = getenv("TMUX_PANE");
    if (valid_pane_id(pane)) {
        free(a->tmux_pane);
        a->tmux_pane = nf_strdup(pane);
    }
}

void nf_tmux_restore(const nf_app *a)
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

#endif /* _WIN32 */