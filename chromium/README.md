# The Chromium fork

**Status:** `[Decided]` topology and procedure (decisions
0001,
0012,
0013)
**Implementation status:** `[Current]` this directory, the
`tools/chromium/*` commands, the sole `src/taffy` mount contract, branding,
string catalogues, and patch specifications exist. `chromium/REVISION` is
pinned to the current Android stable milestone (see [REVISION](REVISION)).
The hard cut from `//components/taffy` to `//taffy` **is** backed now:
`taffy_public_apk` was built from the migrated graph, installed and launched on
a phone, its unit suite passes there, and the same suite passes on a host under
LeakSanitizer with zero reports —
verification report section 0
holds those commands and their output, and the dated sections after it record
the device passes since.

Read that for what it is. It is a build, a launch and a unit-suite result. No
browsertest has been run against the migrated graph at all: the only
browsertest run recorded anywhere in this repository is
`taffy-core/test/TEST-INDEX.md` section
2.1 — 2026-08-20, 219 tests, 195 passed, 24 failed — which predates the
2026-08-22 cut and exists so that a new failure can be told from an old one.
Under decision
0023 point 4, the evidence of
record is a run whose command, host, and output can be named, so a claim about
this fork cites section 0 rather than this paragraph.

For the size of the downstream delta, run `./tools/check fast --only chromium`.
It reports realised patches and modified upstream lines against the fork-debt
budget from the numbers in `chromium/patches/`, so it is never stale; a figure
copied into prose here would be.

**As this is written that lane fails, and the failure is the fork-debt
projection** — which is the one thing worth saying here in words, because the
digits belong to the command. The realised queue sits exactly at the bound, a
warning; the projection is over it, and the lane prints the overshoot as a
percentage and refuses. The whole of it is one unwritten specification, 0008,
deferred to M5; the other two specifications are retired and are not projected
at all. So the way back inside is retiring or refactoring that specification,
not writing more patches, and while realised sits at the bound no new
upstream-file edit is admissible. Run the command for the current figures.

What is built is not what is measured. Cold start, memory, battery, thermal
behaviour and every benchmark scenario remain unmeasured, and nothing has been
signed with a release key — `release-arm64` output carries Chromium's default
debug key, and the signing backend does not exist (see
release runbook). So
nothing in this tree may be described as release-verified.
**Authority:** Chromium fork and build strategy
— that document decides policy; this file is the operating procedure.

## What is committed here

| Path | What it is |
|---|---|
| [`REVISION`](REVISION) | The immutable upstream stable-tag commit. The pin. |
| [`SECURITY_PATCH_LEVEL`](SECURITY_PATCH_LEVEL) | Bumped by the urgent-security fast path; reset at each rebase. |
| [`args/`](args/) | The three committed GN args profiles. Developers never hand-edit GN args. |
| [`patches/`](patches/README.md) | Numbered specifications of the upstream edits and the exported `.patch` files realising them, plus `patches/security/`. `./tools/check fast --only chromium` counts both. |
| [`../taffy-core/`](../taffy-core/README.md) | The complete first-party product root, symlink-mounted into the checkout at `//taffy`. |

The checkout itself is **not** in this repository and never will be: it is a
pristine, gclient-managed workspace outside the repo tree, reproducible from
exactly three inputs — `REVISION`, `patches/`, and `taffy-core/`. A full
chromium/src fork is not practically hostable on ordinary Git hosting, and
patch files keep the entire downstream delta reviewable in ordinary PRs.

## The product targets

`taffy_public_apk`, `taffy_public_test_apk` and `taffy_public_bundle`
(decision 0012), application id `com.taffygo.browser` with `.debug` appended
for debug builds. They are declared in
[`../taffy-core/app/android/BUILD.gn`](../taffy-core/app/android/BUILD.gn), in
the Android product-assembly directory, because a product
target is the top of the build graph: it assembles every subdirectory and is
assembled by none of them.

That file holds the one deliberate layering exception in `//taffy`
— an `import` of the Chrome Android product template — and says at length why
a product target naming `//chrome` is not the same thing as a library naming
`//chrome`. Every other file in the component is forbidden to reach the
embedder, and `DEPS` enforces it.

Upstream `chrome_public_apk` remains a clean-host baseline for proving the
toolchain. It is never shipped or branded as the product.

## Prerequisites

x86-64 Linux, 8+ physical cores, 64 GB RAM recommended (32 GB minimum), and
≥ 400 GB free on a 1 TB NVMe volume for the checkout, git cache, output
directories, and build caches. macOS and Windows are not supported build hosts
for Chromium; run the build on an x86-64 Linux machine, a remote one
included.

## First checkout

```bash
./tools/doctor                                     # read-only: host, disk, tools, pins
./tools/bootstrap --profile chromium --workspace /srv/chromium-taffy
./tools/chromium/sync
./tools/chromium/build --profile dev-x64
./tools/chromium/run
```

`bootstrap` installs pinned depot_tools, runs Chromium's
`install-build-deps.sh`, creates the shared git cache, installs the compiler
caches, and records the workspace path in `.taffy/workspace.env` — so the
later commands never ask again. It is idempotent: re-run it after a
[TOOLCHAIN.md](../TOOLCHAIN.md) bump.

`sync` is the only command that touches the checkout's Git state. It fetches
exactly `REVISION`, applies the patch queue onto the `taffy/patched` branch,
recreates the overlay symlinks, and runs `gclient runhooks`.

`build` writes the committed args profile to `out/<profile>/args.gn`, runs
`gn gen`, and builds `taffy_public_apk` by default.

## Daily loop

```bash
./tools/chromium/build --profile dev-x64    # gn gen from the committed profile + autoninja
./tools/chromium/run                        # incremental install + launch
./tools/chromium/test                       # Taffy suites; --upstream adds affected upstream suites
```

Editing anything under `taffy-core/` needs no sync: the `src/taffy` mount is a
symlink, so the checkout already sees your edits.

Two checks in the overlay run on any host, with no checkout at all, and are
worth running before a build rather than after one:

```bash
python3 taffy-core/resources/catalog/tools/check_strings.py
python3 taffy-core/resources/branding/tools/write_upstream_provenance.py --print
```

The first is the PAR-L10N-001 gate — every string externalized, both
pseudo-locales round-tripping. The second prints exactly what the build will
report about which upstream revision it is.

## Editing an upstream file

The patch queue is the expensive option — always ask whether a new file under
`//taffy` plus a small hook would do instead. Write the
specification first: `chromium/patches/NNNN-short-name.md` names the upstream
file, the symbol, the change, why the overlay cannot host it, and the rebase
risk. It is reviewed on its own and keeps its number when the patch is
exported.

```bash
./tools/chromium/sync                       # leaves you on the taffy/patched branch
# edit upstream files in the checkout; commit each logical change separately,
# with an owner and a reason in the commit message
./tools/chromium/export-patches             # regenerates chromium/patches/ from those commits
./tools/check fast                          # reports patch count and modified upstream lines
```

Then commit the regenerated `.patch` files in this repository. Reviewers read
the delta here, not in a fork branch somewhere else. `export-patches` refuses
to write if the branch and the queue have diverged in a way it cannot
reconcile — resync rather than hand-editing a patch file.

To adopt someone else's queue change without a full re-sync:

```bash
git pull && ./tools/chromium/import-patches
```

## Milestone rebase (~every 4 weeks)

Rebase to each new Chromium stable milestone within 5 working days of the
upstream stable date. There is no continuous rehearsal and no hosted
continuous integration (decision
0023), so the rehearsal is a
command a person runs: `./tools/chromium/import-patches --onto <commit>`,
which replays the queue in a `git worktree` of its own and reports each patch
as clean, auto-merged or failed. **One rehearsal is on record**, 2026-08-20
against 153.0.8009.4: a twenty-patch queue, five conflicts of which two were
substantive, about 27 minutes of hand-resolution, and nothing built, linked or
tested at the new milestone
(chromium fork and build
section 6, item 17). Compile-and-test cost at a new milestone is therefore
still unmeasured, and no rehearsal has been run at the queue's current size.

1. Update `REVISION` (`milestone`, `tag`, `commit`) in a branch of its own.
2. `./tools/chromium/sync --allow-rebase` — reports each patch that fails,
   with its owner and reason, instead of a wall of `git am` output.
3. Fix conflicts as commits on `taffy/patched`, then
   `./tools/chromium/export-patches`.
4. Delete `patches/security/*` folded into the new revision and reset
   `SECURITY_PATCH_LEVEL` to `level=0`.
5. `./tools/chromium/build --profile release-arm64` and the Chromium lane.
6. Record conflicts, elapsed engineer-time, binary size, and startup in the
   rebase report. Exceeding the fork-debt budget blocks new upstream edits
   until the delta shrinks.

Nothing needs to be regenerated by hand at step 1. The build's report of which
upstream revision it is comes from `REVISION`, `SECURITY_PATCH_LEVEL` and the
patch queue through
`taffy-core/resources/branding/tools/write_upstream_provenance.py`, which
emits a depfile so the regeneration is Ninja's problem rather than a person's.

## Urgent security fast path

Do not rebase the milestone. Cherry-pick, bump, gate, ship, fold out — the
full procedure is in
[`patches/security/README.md`](patches/security/README.md).

Bumping `SECURITY_PATCH_LEVEL` is what makes the fix visible in the product:
the level and the advisory references it names are two of the rows the version
surface shows (PAR-SEC-002), and the generator refuses a level above zero with
no advisories recorded.

## Licensing and branding

Chromium is BSD-licensed with additional third-party licenses; the fork
inherits every one of those obligations, including generating and shipping the
complete attribution set with the package. Google-owned branding, the Chrome
name and logo, and Google-only APIs and services are **not** included in a
fork's grant: TaffyGo ships its own name, icons, and about-page attribution,
and treats Google services, codecs/DRM, and Safe Browsing as separately owned
launch workstreams tracked in the
open-decisions register.

The replacement is one directory:
[`../taffy-core/resources/branding/`](../taffy-core/resources/branding/README.md).
The product name is an Android resource with overlay precedence, the
application id is a build value, and the launcher icon set is derived at build
time from the committed mark in [`brand/`](../brand/README.md) rather than
committed as artwork. Nothing under `branding/` contains a colour value: the
palette is
record 0025's, and the
icon layers that need a flat brand colour — adaptive, round, and the store
listing icon — are named as undrawn items in that directory's README rather
than guessed at. The colour they need now exists; the artwork does not.

Every user-visible string TaffyGo owns is externalized in
[`../taffy-core/resources/catalog/`](../taffy-core/resources/catalog/README.md)
and covered by a pseudo-localization gate that runs on any host
(PAR-L10N-001).

## Known disagreements, resolved

This section was a ledger of contradictions between this directory and
documents it does not own, recorded here rather than silently reconciled. All
three entries are now resolved in the owning documents; the resolutions are
kept so the next disagreement has a precedent to follow.

1. **Component build in the dev profiles.** Decision 0013 §"Build
   acceleration" item 4 and
   the fork strategy §3
   item 4 both showed `is_component_build = true` for the dev profiles,
   carrying over a desktop Chromium result. `//build/config/BUILDCONFIG.gn`
   asserts that a component build is impossible on Android. **Resolved:** both
   documents now show `is_component_build = false` for every profile and say
   why, and `./tools/check fast` fails a dev profile that sets it.
2. **Where the product targets are declared.** The fork strategy §1.2 said
   `//taffy/app/android/BUILD.gn`. **Resolved:** it now names the
   overlay's root `BUILD.gn`, for the reason given under "The product targets"
   above.
3. **"Resolved at SP-01" claims.** Comments in `chromium/args/` that described
   the component-build question as resolved by a failed `gn gen` were
   corrected to describe it as a reading of upstream's own assertion. A
   `gn gen` has since run on every committed profile and none of them sets
   `is_component_build`, so the reading holds. **Resolved:** the equivalent
   comment in `tools/check` was reworded the same way — it now records the old
   rule's retirement and cites upstream's assert rather than claiming a
   measurement that file never made.
