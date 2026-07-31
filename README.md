# pikostore — Software Center

The piko ROM's own GUI for managing what is installed on it, for a Sharp
Zaurus SL-C760/C860 running the [piko](https://github.com/sugarkrap/piko)
ROM. FLTK 1.3 on X11 (Xfbdev), C++98, no other dependencies.

This repo is consumed as a git submodule at `userspace/src/pikostore` in
the piko tree, and is built and shipped by `tools/build-pikostore.sh`
there. It is not useful on its own: it reads `/etc/zaurus/manifest` and
drives `/usr/sbin/piko-update`, neither of which exists off-device.

## Tabs

**Packages** — a stub. This will become an ipkg front end.

**System Update** — the working half:

- the running kernel (`uname -r`)
- this ROM's version and changelog, from `/etc/zaurus/manifest`
- a table of every update installed with this tool, from
  `/etc/zaurus/update-history` — ROM version, install date, and a Revert
  button that is always disabled for now
- an **Update…** button that picks a `.tar` package (defaulting to
  `/mnt/card`, the SD card) and installs it with a live progress bar and
  the updater's output in a console box

## How it talks to piko-update

Two pipes, not one:

| stream | fd | carries |
|---|---|---|
| stdout + stderr | 1, 2 | human text, shown verbatim in the console box |
| progress | 3 | `TOTAL` / `PROGRESS` / `STATUS` / `DONE` records |

Keeping them separate means the console can show everything the updater
says without a filter, while the progress bar reads something it can
actually parse. Unknown progress records are ignored, so `piko-update`
can add new ones without breaking an older build of this app.

The child is always run with `--no-reboot`. piko-update's default is to
reboot immediately on success, which from a GUI would tear down the window
showing the progress before anyone could read it. The reboot becomes a
button instead.

## Building

Normally:

```sh
tools/build-pikostore.sh          # from the piko repo
```

Directly, against an already-staged FLTK:

```sh
make STAGE=/path/to/userspace/stage-target \
     CXX=arm-unknown-linux-uclibcgnueabi-g++
```

## Notes

- Runs as root — the X session is started from `inittab` and inherits its
  uid, which is what `piko-update` needs.
- Targets 640×480. Every tab sets a `resizable()` so Matchbox can resize
  or fullscreen the window.
- The "oups, no changelog found, sowy" string is intentional, typo and
  all. Please leave it.
