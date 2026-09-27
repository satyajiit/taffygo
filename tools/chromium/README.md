# `tools/chromium/`

**Status:** `[Current]` — the commands exist and `./tools/chromium/security-patch
--self-test` passes on any host; the build, run and test commands need the
x86-64 Linux track and a checkout, and say so when they are run elsewhere.
**Owning milestone:** M0 for the four-command developer path, M1 (WP-M1-08) for
the urgent-security fast path.
**Authority boundary:** the commands that operate the Chromium checkout. Every
procedure they implement is specified in the
fork and build strategy;
this directory implements it and adds nothing of its own. The command index for
the whole repository is [`../README.md`](../README.md).

| Command | What it does |
|---|---|
| [`sync`](sync) | Checkout at exactly `chromium/REVISION`, patch queue onto `taffy/patched`, overlay mount, hooks |
| [`build`](build) | `gn gen` from a committed args profile, then `autoninja` |
| [`run`](run) | Incremental install and launch on the connected device |
| [`test`](test) | The Taffy suites; `--upstream` adds the affected upstream suites |
| [`orphan-targets`](orphan-targets) | Every GN target under `//taffy` is built by something, or named as debt |
| [`export-patches`](export-patches) | Regenerate `chromium/patches/` from the branch — the only writer of the queue |
| [`import-patches`](import-patches) | Re-apply the queue; `--onto` is the rebase rehearsal |
| [`security-patch`](security-patch) | The urgent-security fast path and its timing record |
| [`lib/`](lib/chromium.sh) | `chromium.sh` for the checkout helpers, `security_queue.sh` for the cherry-pick queue and the patch level, `drill.py` and `drill_cli.py` for the response record |

## The urgent-security fast path

For an actively exploited critical fix the milestone is **not** rebased. The
upstream commit is cherry-picked onto the current pinned revision, exported into
`chromium/patches/security/`, `chromium/SECURITY_PATCH_LEVEL` is bumped, and the
Chromium and release lanes run against the existing baseline — the procedure in
fork and build §1.4 and
testing and delivery §12. The
operating procedure around it, including who is called at 3am, is the
release and incident runbook.

`security-patch` measures the response and never grades it. The response-time
values in `PAR-SEC-003` are candidate objectives owned by OD-052, ratified from
repeated drills; a tool that printed "within target" would be deciding an open
question by writing a number into a report.

```bash
./tools/chromium/security-patch open  --advisory <ref> --severity critical \
    --kind drill --exploited --record artifacts/security-drill/<ref>.json
./tools/chromium/security-patch apply --from-commit <upstream-sha> --record <file>
./tools/chromium/security-patch mark  --event artifact_verified --record <file>
./tools/chromium/security-patch report --record <file> --fragment drill.json
./tools/chromium/security-patch fold  # at the next milestone rebase
```

## What this directory does not do

1. **It does not build on macOS or Windows.** Chromium's Android build is
   x86-64 Linux only; `sync`, `build`, `run` and `test` refuse rather than
   producing a broken tree. Record-keeping in `security-patch` runs anywhere,
   deliberately: the clock starts when the advisory is seen, not when someone
   reaches a Linux machine.
2. **It never hand-edits a patch file.** The editing surface is the
   `taffy/patched` branch; `export-patches` is the only writer of the queue.
   `security-patch apply --from-file` is the one exception and it validates that
   what it is given is a git mailbox patch whose subject carries the marker
   `export-patches` uses to route it back here — a patch that fails either check
   would silently leave the queue at the next export.
3. **`orphan-targets` is not a lane of `./tools/check fast`, on purpose.** It
   needs a synced checkout and a generated `out/` directory, so on every other
   host it could only skip, and a check that skips everywhere is a check people
   learn to ignore. The lane-able half — is a `BUILD.gn` loaded at all — is
   `taffy-core/build/tools/check_build_reachability.py` and
   does run in `fast`; this command answers the question that one cannot, which
   is whether a **target** inside a loaded file is built by anything. Two
   suites, forty tests between them, sat in that gap until 2026-08-19.
4. **It never signs or promotes an artifact.** That is
   [`../release`](../release) and the release CI lane.
