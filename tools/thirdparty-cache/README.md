# Vendored third-party source tarballs

`.github/workflows/build-ipk.yml` needs `piko/tools/build-thirdparty-deps.sh`
to succeed, which downloads eight pinned tarballs from their upstream
mirrors into `userspace/.thirdparty-cache/` before building anything
(`fetch()` in that script skips the download entirely if the file is
already there, then verifies its sha256 either way — see that script).

Two consecutive CI runs (2026-08-04) failed partway through that step on
transient errors from two *different* upstream hosts:

| package | host | error seen in CI |
|---|---|---|
| fontconfig 2.14.2 | www.freedesktop.org | `curl: (22) ... 418` |
| freetype 2.13.2 | download.savannah.gnu.org | `curl: (22) ... 502` |

Independently reproduced from this session's own network: `savannah.gnu.org`,
`www.freedesktop.org`, `gitlab.freedesktop.org` and `www.x.org` all
currently return `403` to a plain `curl`, while GitHub- and
SourceForge-hosted downloads succeed without issue. Whatever is causing
it, it is not this project's code, and every retry costs a full
cross-toolchain rebuild first (~70-80 min) before even reaching this step
-- expensive to keep hitting blind.

## What's vendored here, and what isn't

Six of the eight tarballs `build-thirdparty-deps.sh` pins, fetched from a
mirror that worked and verified byte-for-byte against the exact sha256
pinned in that script:

| file | sha256 | source used |
|---|---|---|
| zlib-1.3.1.tar.gz | `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` | github.com/madler/zlib releases (same as the pin) |
| expat-2.6.2.tar.gz | `d4cf38d26e21a56654ffe4acd9cd5481164619626802328506a2869afab29ab3` | github.com/libexpat/libexpat releases (same as the pin) |
| libpng-1.6.43.tar.gz | `e804e465d4b109b5ad285a8fb71f0dd3f74f0068f91ce3cdfde618180c174925` | download.sourceforge.net (same as the pin) |
| freetype-2.13.2.tar.gz | `1ac27e16c134a7f2ccea177faba19801131116fd682efc1f5737037c5db224b5` | **SourceForge mirror**, not the pinned savannah.gnu.org URL -- byte-identical, sha256 matches the pin exactly |
| libarchive-3.7.7.tar.gz | `4cc540a3e9a1eebdefa1045d2e4184831100667e6d7d5b315bb1cbc951f8ddff` | github.com/libarchive/libarchive releases (same as the pin) |
| dejavu-fonts-ttf-2.37.tar.bz2 | `fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7` | github.com/dejavu-fonts/dejavu-fonts releases (same as the pin) |

**Not vendored** -- every alternate mirror tried from this session
(gitlab.freedesktop.org, fossies.org, Gentoo/Arch distfile mirrors) was
either also blocked or unreachable from here:

- `fontconfig-2.14.2.tar.gz` (pin: `3ba2dd92158718acec5caaf1a716043b5aa055c27b081d914af3ccb40dce8a55`)
- `xkeyboard-config-2.32.tar.bz2` (pin: `1feee317ba39b91902b0cbd2987c0c73e6afbfc8f4c096367a5c86c216c036a8`)

`build-ipk.yml` still fetches these two live, so the workflow can still hit
the same flakiness on them specifically until someone with unblocked
network access downloads and verifies them (against the sha256s above) and
adds them here the same way.

## Updating

If `piko/tools/build-thirdparty-deps.sh` ever bumps one of these pinned
versions, re-derive the new pin from that script and replace the matching
file here (or drop it, and let the workflow fall back to a live download).
An sha256 mismatch is not silently tolerated -- `fetch()` in that script
refuses to build against a tarball whose hash doesn't match its pin, from
a vendored copy exactly the same as from a fresh download.
