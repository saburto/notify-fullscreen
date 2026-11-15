/*
 * Layout and painting. Portable: draws into any cairo context, so every
 * backend only has to provide a surface.
 */

#include "nf.h"

#define PI 3.14159265358979323846

/* palette */
#define COL_BG_R 0.016
#define COL_BG_G 0.020
#define COL_BG_B 0.027
#define COL_TEXT_R 0.965
#define COL_TEXT_G 0.973
#define COL_TEXT_B 0.984
#define COL_OK_R 0.153
#define COL_OK_G 0.729
#define COL_OK_B 0.341
#define COL_OK_HOVER_R 0.239
#define COL_OK_HOVER_G 0.843
#define COL_OK_HOVER_B 0.451
#define COL_NO_R 0.169
#define COL_NO_G 0.196
#define COL_NO_B 0.231
#define COL_NO_HOVER_R 0.239
#define COL_NO_HOVER_G 0.275
#define COL_NO_HOVER_B 0.325

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h,
                         double r)
{
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, PI / 2, PI);
    cairo_arc(cr, x + r, y + r, r, PI, 3 * PI / 2);
    cairo_close_path(cr);
}

static void draw_label(cairo_t *cr, const char *label, int font_px, double bx,
                       double by, double bw, double bh)
{
    cairo_set_font_size(cr, font_px);
    cairo_text_extents_t te;
    cairo_text_extents(cr, label, &te);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_move_to(cr, bx + (bw - te.x_advance) / 2.0 - te.x_bearing,
                  by + bh / 2.0 - te.y_bearing - te.height / 2.0);
    cairo_show_text(cr, label);
}

static void wrap_at(nf_app *a, cairo_t *cr, int fs, double maxw)
{
    cairo_set_font_size(cr, fs);
    nf_lines_free(&a->msg);
    nf_wrap_text(&a->msg, cr, a->text, maxw);
}

/*
 * Wrap the message and pick a font size: short messages get a bigger font,
 * very long ones are shrunk until they fit above the button.
 * Leaves the message wrapped and `cr`'s font size set to the result.
 */
static int fit_message(nf_app *a, cairo_t *cr, int fs, double maxw,
                       double max_h)
{
    const int explicit_size = a->font_px > 0;
    const int cap = nf_clampi((int)(a->h * 0.09), fs, 240);
    const double lh_ratio = 1.34;

    if (max_h < 40)
        max_h = a->h * 0.3;

    wrap_at(a, cr, fs, maxw);

    if (!explicit_size && strlen(a->text) <= 140) {
        for (int i = 0; i < 4; i++) {
            double widest = 0;
            for (size_t k = 0; k < a->msg.n; k++) {
                double w = nf_measure(cr, a->msg.v[k]);
                if (w > widest)
                    widest = w;
            }
            if (widest <= 0)
                break;
            const double want = a->w * 0.55;
            if (widest >= want * 0.92)
                break;
            int next = (int)(fs * (want / widest));
            if (next > cap)
                next = cap;
            if (next <= fs)
                break;
            fs = next;
            wrap_at(a, cr, fs, maxw);
        }
    }

    while (fs > 9 && a->msg.n * (fs * lh_ratio) > max_h) {
        fs = (int)(fs * 0.9);
        wrap_at(a, cr, fs, maxw);
    }

    cairo_set_font_size(cr, fs);
    return fs;
}

void nf_paint(nf_app *a, cairo_t *cr)
{
    const int W = a->w, H = a->h;

    /* dimmed (or opaque, without a compositor) background */
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, COL_BG_R, COL_BG_G, COL_BG_B,
                          a->translucent ? a->opacity : 1.0);
    cairo_paint(cr);
    cairo_restore(cr);

    /* message */
    cairo_select_font_face(cr, NF_FONT_FAMILY, CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);

    int fs = a->font_px > 0 ? a->font_px
                            : nf_clampi((int)(H * 0.052), 16, 160);
    if (a->font_px <= 0 && W < 900)
        fs = nf_clampi((int)(fs * 0.8), 14, 160);
    fs = fit_message(a, cr, fs, W * 0.80, H * 0.62);

    /* the buttons are sized from the message font; shrink both until it fits */
    int bf = 0;
    double yes_w = 0, no_w = 0, row_w = 0, bgap = 0, btn_h = 0, gap = 0,
           lh = 0, msg_h = 0, total = 0;
    for (int iter = 0;; iter++) {
        bf = nf_clampi((int)(fs * 0.45), 14, 72);
        cairo_set_font_size(cr, bf);

        btn_h = bf * 2.4;
        const double pad = btn_h * 1.9;
        const double min_w = btn_h * 2.8;

        yes_w = nf_measure(cr, a->ok_label) + pad;
        if (yes_w < min_w)
            yes_w = min_w;

        if (a->confirm) {
            no_w = nf_measure(cr, a->no_label) + pad;
            if (no_w < min_w)
                no_w = min_w;
            if (no_w > yes_w)
                yes_w = no_w;
            no_w = yes_w;
            bgap = btn_h * 0.35;
            const double avail = W * 0.82 - bgap;
            if (yes_w > avail / 2)
                yes_w = no_w = avail / 2;
            row_w = yes_w * 2 + bgap;
        } else {
            if (yes_w > W * 0.55)
                yes_w = W * 0.55;
            row_w = yes_w;
        }

        gap = btn_h * 0.85;
        lh = fs * 1.34;
        msg_h = a->msg.n * lh;
        total = msg_h + gap + btn_h;

        if (total <= H * 0.90 || iter > 12 || fs <= 9)
            break;

        fs = (int)(fs * 0.93);
        wrap_at(a, cr, fs, W * 0.80);
    }

    double top = (H - total) / 2.0;
    if (top < H * 0.05)
        top = H * 0.05;

    /* message lines, centered one by one */
    cairo_set_font_size(cr, fs);
    cairo_set_source_rgb(cr, COL_TEXT_R, COL_TEXT_G, COL_TEXT_B);
    double y = top;
    for (size_t i = 0; i < a->msg.n; i++) {
        cairo_text_extents_t te;
        cairo_text_extents(cr, a->msg.v[i], &te);
        cairo_move_to(cr, (W - te.x_advance) / 2.0 - te.x_bearing,
                      y + lh / 2.0 - te.y_bearing - te.height / 2.0);
        cairo_show_text(cr, a->msg.v[i]);
        y += lh;
    }

    /* buttons */
    const double radius = btn_h * 0.26;
    const double row_x = (W - row_w) / 2.0;
    const double by = top + msg_h + gap;

    a->btn[0].x = row_x;
    a->btn[0].y = by;
    a->btn[0].w = yes_w;
    a->btn[0].h = btn_h;

    a->btn[1].x = row_x + yes_w + bgap;
    a->btn[1].y = by;
    a->btn[1].w = no_w;
    a->btn[1].h = btn_h;

    /* primary button: green (OK / Yes) */
    if (a->sel == 1)
        cairo_set_source_rgb(cr, COL_OK_HOVER_R, COL_OK_HOVER_G,
                             COL_OK_HOVER_B);
    else
        cairo_set_source_rgb(cr, COL_OK_R, COL_OK_G, COL_OK_B);
    rounded_rect(cr, a->btn[0].x, a->btn[0].y, a->btn[0].w, a->btn[0].h,
                 radius);
    cairo_fill(cr);
    draw_label(cr, a->ok_label, bf, a->btn[0].x, a->btn[0].y, a->btn[0].w,
               a->btn[0].h);

    if (a->confirm) {
        /* secondary button: neutral */
        if (a->sel == 2)
            cairo_set_source_rgb(cr, COL_NO_HOVER_R, COL_NO_HOVER_G,
                                 COL_NO_HOVER_B);
        else
            cairo_set_source_rgb(cr, COL_NO_R, COL_NO_G, COL_NO_B);
        rounded_rect(cr, a->btn[1].x, a->btn[1].y, a->btn[1].w, a->btn[1].h,
                     radius);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
        cairo_set_line_width(cr, 1.5);
        cairo_stroke(cr);
        draw_label(cr, a->no_label, bf, a->btn[1].x, a->btn[1].y, a->btn[1].w,
                   a->btn[1].h);

        /* focus ring around the selected button */
        const nf_rect *sel = &a->btn[a->sel == 2 ? 1 : 0];
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
        cairo_set_line_width(cr, 2.0);
        rounded_rect(cr, sel->x - 4, sel->y - 4, sel->w + 8, sel->h + 8,
                     radius + 4);
        cairo_stroke(cr);
    }
}