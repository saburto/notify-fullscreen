# notify-fullscreen

A tiny fullscreen notifier for Linux: it dims the screen, shows a message and
waits until you acknowledge it with a green **OK** button.

It is meant for the things you *must* see — a finished long build, a failed
cron/backup job, a deploy that went wrong — without a notification daemon and
without growing into a framework.

```
$ notify-fullscreen "Deployment of api-gateway v2.4.1 failed on prod-01: health check timeout"
```

![preview](docs/preview.png)

<sub>The overlay rendered with `--preview-png`: the background is 85% opaque,
so the desktop behind it stays visible.</sub>

* **40 KB** single binary, ~14 MB RSS (almost all of it shared system libs),
  ~10 ms until the overlay is on screen. No daemon, no config file, no runtime.
* **Translucent background** (85% opaque by default) so you still see what is
  behind the message.
* **Fullscreen and always on top**, above every window, with keyboard focus.
* Plain **X11 / Xwayland** — works on GNOME/KDE Wayland sessions (which ship
  Xwayland), on Hyprland, sway, i3, XFCE and friends.
* Message is **wrapped and auto-sized**: short messages get a big font, long
  ones shrink to fit.

## Build & install

```sh
sudo apt install libx11-dev libcairo2-dev   # Debian/Ubuntu
make                                        # -> ./notify-fullscreen
make install                                # -> ~/.local/bin/notify-fullscreen
```

`make install` honours `PREFIX` (default `$(HOME)/.local`), so
`sudo make install PREFIX=/usr/local` works too. `make uninstall` removes it.

Only two shared libraries are required at runtime: `libX11` and `libcairo`
(cairo pulls in FreeType/fontconfig for text).

## Usage

```
usage: notify-fullscreen [options] MESSAGE

  -o, --opacity FLOAT     background opacity 0.0-1.0 (default 0.85)
  -f, --font-size PX      message font size in pixels (default: screen based)
      --ok-label TEXT     button label (default "OK")
  -h, --help              show this help and exit
  -V, --version           print version and exit

MESSAGE may span several arguments (joined with spaces);
use "-" to read the message from standard input.
```

Examples:

```sh
# short alert
notify-fullscreen "Backup finished"

# several arguments are joined, newlines and blank lines are kept
notify-fullscreen "Deploy failed" "(prod-01)"
printf 'Restore complete\nSnapshot 42\n' | notify-fullscreen -

# lighter dim, custom button
notify-fullscreen --opacity 0.6 --ok-label "Got it" "Disk /var is 92% full"

# long text: wrapped automatically
notify-fullscreen "Backup job failed after 3 retries: destination unreachable, last error: connection reset by peer while uploading archive segment 7 of 42"
```

Dismiss it with the green **OK** button, `Enter`, `Space` or `Esc`
(`q`/`y` also work). The mouse cursor stays hidden until it hovers the button.

Exit status: `0` acknowledged · `2` usage error · `1` no X display
· `130` interrupted (SIGINT/SIGTERM/SIGHUP — the window is cleaned up).

## How it is used from scripts

```sh
#!/bin/sh
if ! ./deploy.sh; then
    notify-fullscreen "Deploy failed on $(hostname) at $(date +%H:%M)"
fi
```

Since it blocks until acknowledged, run it in the background if the script
must continue:

```sh
notify-fullscreen "...deploy failed..." &
```

## Notes

* **Why X11/Xwayland and not a Wayland-native client?** A Wayland client needs
  a layer-shell protocol (not exposed to normal apps) to cover the screen
  reliably; through Xwayland the same thing is one window plus two EWMH hints,
  and it stays dependency-free. If your session has no Xwayland (`$DISPLAY`
  unset) the tool tells you so and exits `1`.
* Without a compositing manager there is no real transparency; the overlay then
  falls back to an opaque dark background, everything else works the same.
* Keyboard focus: the overlay asks the window manager to activate it. Some
  setups with strict focus-stealing prevention may keep focus elsewhere — the
  OK button always works with the mouse.
* `--preview-png FILE [--preview-size WxH]` renders the overlay to a PNG
  instead of showing it; handy for screenshots/docs, and it works headless when
  `--preview-size` is given.

## License

MIT — see [LICENSE](LICENSE).