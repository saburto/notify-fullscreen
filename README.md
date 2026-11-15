# notify-fullscreen

> **Personal project — Linux only.** Built for my own workflow, tested on
> Ubuntu 26.04 (GNOME/Wayland + Xwayland), maintained best-effort, no support
> promised. No Windows or macOS backend yet; other distributions/sessions are
> untested. Fork it if you need something else.

A tiny fullscreen notifier for Linux (X11/Xwayland). It dims the screen and
shows a message until you dismiss it with the green **OK** button — or asks a
**Yes/No** question and reports the answer as an exit code.

![preview](docs/confirm.png)

## Install

```sh
sudo apt install libx11-dev libcairo2-dev   # Debian/Ubuntu
make && make install                        # -> ~/.local/bin/notify-fullscreen
```

Runtime deps: `libX11` + `libcairo` only. ~40 KB binary, ~14 MB RSS, no config,
no daemon. On Wayland it needs Xwayland (`$DISPLAY` set). `PREFIX=/usr/local`
and `make uninstall` work too.

## Usage

```sh
notify-fullscreen "Deploy failed on prod-01"
notify-fullscreen --confirm "Deploy to production?"   # 0 = Yes, 1 = No
notify-fullscreen -r "Build done"                     # focus back (+ tmux pane)
printf 'line 1\nline 2\n' | notify-fullscreen -       # message on stdin
```

```
-c, --confirm          Yes/No buttons: Yes exits 0, No / Esc / n exits 1
-r, --refocus          return focus to the triggering window and tmux pane
-o, --opacity FLOAT    background opacity 0.0-1.0 (default 0.85)
-f, --font-size PX     message font size in pixels (default: screen based)
    --ok-label TEXT    primary button label ("OK", or "Yes" with -c)
    --no-label TEXT    secondary button label ("No", only with -c)
-h, --help   -V, --version
```

Dismiss with the button, `Enter`/`Space`, `Esc`, `y`/`n`; in Yes/No mode
`Tab`/`←`/`→` move the selection.

Exit: `0` OK/Yes · `1` No · `2` usage · `3` runtime error · `130` interrupted.

## Notes

- **Linux only** — X11/Xwayland, tested on Ubuntu 26.04 (GNOME/Wayland).
  No Wayland-native, Windows or macOS backend exists yet; ports plug into
  [`src/backend.h`](src/backend.h). Other distros/sessions are untested.
- Without a compositor the background is opaque instead of translucent.
- `--preview-png FILE [--preview-size WxH]` renders the overlay to a PNG
  (works headless).

MIT — see [LICENSE](LICENSE).