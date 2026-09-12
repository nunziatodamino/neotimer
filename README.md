# neotimer

A small, native Linux countdown with bold block digits, cyan accents, and a progress bar.

```sh
neotimer 45m
neotimer 30s
neotimer 2h
```

The display follows the remaining time: `01:00:00` becomes `59:59`, then `59`, and finally `00`.
It recenters when the format or terminal size changes, with a compact layout for small windows.
The large digits use solid blocks in UTF-8 terminals, with thick ASCII strokes in other locales.

Press **Space** to pause or resume, **Esc** to exit, or **Ctrl+C** to cancel.
The previous terminal screen and settings are restored on exit.

## Build and install

Requires Linux, a C compiler, and Make. Python 3 is needed only for integration tests.

```sh
make
make test
make install
```

The default installation is `~/.local/bin/neotimer`. Ensure that directory is on your PATH:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Add that line to your shell startup file if needed. You can then run `neotimer` from any directory.
To choose a different installation location, use `make install PREFIX=/your/prefix`.
Use `make uninstall` with the same prefix to remove the installed binary.

## Behavior

- Supply one positive integer immediately followed by lowercase `s`, `m`, or `h`.
  Fractions, combined durations, zero, and negative durations are rejected.
  The maximum duration is 9,223,372,036 seconds (or the largest whole minutes/hours within that limit).
- Timing uses a monotonic clock and excludes paused time. The event loop sleeps between updates.
- At completion, a message is printed and an optional desktop notification is sent using `notify-send`.
  No terminal bell is emitted. A silent hint is sent to the desktop notification service;
  the desktop ultimately determines sound behavior. Missing or failed notifications do not fail the timer.
- Set `NO_COLOR=1` to disable colors. When input/output is redirected, `TERM` is unset, or
  `TERM=dumb`, the timer prints start and completion messages without animation or keyboard controls.
- Exit status: `0` for completion or Esc, `2` for invalid arguments, `1` for internal errors,
  and `128 + signal number` for handled termination signals (for example, `130` for Ctrl+C).

Run `neotimer --help` for a quick reference.
