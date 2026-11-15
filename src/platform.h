/*
 * notify-fullscreen — a tiny fullscreen modal notifier
 *
 * Platform glue only. Everything else is shared across backends.
 */

#ifndef NF_PLATFORM_H
#define NF_PLATFORM_H

#define NF_VERSION "1.3.0"

/*
 * Font family for the UI. cairo's "toy" font API resolves this name through
 * the platform font backend: fontconfig (Linux), GDI (Windows) or CoreText
 * (macOS). Keep the names the respective system knows.
 */
#if defined(_WIN32)
#define NF_FONT_FAMILY "Segoe UI"
#elif defined(__APPLE__)
#define NF_FONT_FAMILY "Helvetica Neue"
#else
#define NF_FONT_FAMILY "Sans"
#endif

#endif /* NF_PLATFORM_H */