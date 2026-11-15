/*
 * Command line entry point: parse options, read the message, drive the
 * backend, map the outcome to an exit status.
 */

#include <signal.h>

#include "backend.h"
#include "nf.h"

#if defined(_WIN32)
#include <io.h>
#define nf_isatty(fd) _isatty(fd)
#else
#include <unistd.h>
#define nf_isatty(fd) isatty(fd)
#endif

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
    char *buf = nf_malloc(cap);

    for (;;) {
        if (n + 1 >= cap)
            buf = nf_realloc(buf, cap *= 2);
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

    char *buf = nf_malloc(len + 1);
    buf[0] = '\0';
    for (int i = first; i < argc; i++) {
        if (i > first)
            strcat(buf, " ");
        strcat(buf, argv[i]);
    }
    return buf;
}

static void app_free(nf_app *a)
{
    nf_lines_free(&a->msg);
    free(a->text);
    free(a->ok_label);
    free(a->no_label);
    free(a->tmux_pane);
}

int main(int argc, char **argv)
{
    nf_app a;
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
            printf("notify-fullscreen %s\n", NF_VERSION);
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
            a.no_label = nf_strdup(argv[i]);
            continue;
        }
        if (!strcmp(arg, "--ok-label")) {
            if (++i >= argc) {
                fprintf(stderr, "notify-fullscreen: --ok-label needs a value\n");
                return 2;
            }
            free(a.ok_label);
            a.ok_label = nf_strdup(argv[i]);
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
        a.ok_label = nf_strdup(a.confirm ? "Yes" : "OK");
    if (!a.no_label)
        a.no_label = nf_strdup("No");
    a.sel = a.confirm ? 1 : 0; /* Yes is preselected in yes/no mode */

    nf_tmux_capture(&a); /* $TMUX_PANE, only with --refocus */

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
    } else if (!nf_isatty(0)) {
        a.text = read_stdin();
        if (!a.text) {
            usage(stderr);
            return 2;
        }
    } else {
        usage(stderr);
        return 2;
    }

    /* headless preview: no display needed, fonts come from the platform */
    if (preview_path && preview_w > 0 && preview_h > 0) {
        a.translucent = 1;
        a.w = preview_w;
        a.h = preview_h;
        nf_write_preview(&a, preview_path);
        printf("wrote %s (%dx%d)\n", preview_path, a.w, a.h);
        app_free(&a);
        return 0;
    }

    a.be = nf_backend_new();
    nf_bm_open(a.be);

    if (preview_w > 0) {
        a.w = preview_w;
        a.h = preview_h;
    } else {
        nf_bm_screen_size(a.be, &a.w, &a.h);
    }

    /* preview at the screen size: keep the alpha channel for the PNG */
    if (preview_path) {
        a.translucent = 1;
        nf_write_preview(&a, preview_path);
        nf_bm_close(a.be);
        printf("wrote %s (%dx%d)\n", preview_path, a.w, a.h);
        app_free(&a);
        return 0;
    }

    nf_bm_create_window(a.be, &a);

    signal(SIGINT, nf_signal_handler);
    signal(SIGTERM, nf_signal_handler);
    signal(SIGHUP, nf_signal_handler);

    nf_run(&a);

    nf_bm_restore_focus(a.be, &a);
    nf_tmux_restore(&a);
    nf_bm_destroy(a.be);

    const int stop = nf_should_stop();
    const int result = a.result;
    app_free(&a);
    return stop ? 130 : result;
}