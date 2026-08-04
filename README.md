# pikostore — Software Center

The piko ROM's own GUI for managing what is installed on it, for a Sharp
Zaurus SL-C760/C860 running the [piko](https://github.com/sugarkrap/piko)
ROM. FLTK 1.3 on X11 (Xfbdev), C++98, no other dependencies.

This repo is consumed as a git submodule at `userspace/src/pikostore` in
the piko tree, and is built and shipped by `tools/build-pikostore.sh`
there. It is not useful on its own: it reads `/etc/zaurus/manifest` and
drives `/usr/sbin/piko-update`, neither of which exists off-device.

## Tabs

**Packages** — browsing, installing and removing ipkg packages isn't built
yet, but repo management is: a **Settings…** button (top right) opens a
dialog listing the package-repo manifest URLs this app will look in,
stored at `/etc/zaurus/pikostore-repos` (one URL per line — see
`repostate.h`). A fresh install with no settings file yet defaults to
exactly one repo: this repo's own `packages/manifest.yaml`, on GitHub
raw-content. Add a URL from the input at the bottom left (the **Add**
button lights up once you type something); select a row in the list to
enable **Delete**. Every change is written to disk immediately, no
separate save step.

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

## This repo as a package source

`packages/` holds git submodules for this project's own ROM apps
([zmenunx](https://github.com/sugarkrap/zmenunx),
[otQuake](https://github.com/sugarkrap/otquake),
[otCraft](https://github.com/sugarkrap/otcraft)) plus, once built,
`manifest.yaml` and each package's `.ipk` (git-lfs tracked — see
`.gitattributes`). That manifest is exactly what a fresh pikostore install
points at by default (see the Packages tab above): the store just taps
into this repo's own raw GitHub content, no separate feed server.

`tools/generate-basic-repo.sh` builds it. Run BY HAND, from a checkout with
a sibling `../piko` whose cross-toolchain (and, per package, whatever else
it needs — see the script's own comments) is already built:

```sh
tools/generate-basic-repo.sh
```

A package that fails to build, or has no build prerequisites available, is
listed in `manifest.yaml` without an `ipk:` field rather than aborting the
whole run — the script always finishes and always regenerates the
manifest from whatever did succeed.

`.github/workflows/build-ipk.yml` builds pikostore's *own* `.ipk` (so the
store can be installed on a ROM that didn't ship it pre-baked), by
checking out `sugarkrap/piko` and reusing its toolchain/X11/FLTK build
tooling. It's `workflow_dispatch` and version-tag triggered only — the same
multi-hour-on-cold-cache pipeline `build-piko-zip.yml` runs in the piko
repo, too heavy for every push.

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
