# 0004 — Report the upstream revision and downstream delta on the version surface

**Status:** `[Decided]` that the edit is required; the exact data source and
template are confirmed at SP-01
**Needed by:** PAR-SEC-002, an M1 **Required** row: the build reports the
Chromium version, the patch delta, and the known lag
**Estimated size:** ~45 modified upstream lines, 3 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/ui/webui/version/version_ui.cc` |
| Symbol | the function that populates the version page's data source |
| File | `//chrome/browser/ui/webui/version/version_handler.cc` |
| Symbol | the handler that answers the page's requests |
| File | `//components/version_ui/resources/about_version.html` | 
| Symbol | the template that renders the version rows |

## The change

Add one section to the version page, fed entirely by
`taffy::GetUpstreamProvenance()` from
`//taffy/resources/branding/taffy_upstream_provenance.h`:

| Row | Value |
|---|---|
| Chromium milestone | `chromium_milestone` |
| Chromium version | `chromium_tag` |
| Chromium revision | `chromium_commit` |
| Revision pinned on | `pin_recorded_date`, or the "not recorded" string when empty |
| Downstream changes | `downstream_patch_count` |
| Downstream changed lines | `downstream_modified_upstream_lines` |
| Security fixes applied on top | `security_patch_level` |
| Security references | `security_advisories` |

The labels are `IDS_TAFFY_VERSION_*` from
`//taffy/resources/catalog/strings/taffy_version_strings.grdp`, which is why
patch 0003 is a prerequisite of this one.

The C++ side is a loop over eight label/value pairs; the HTML side is one
repeated row. No logic moves upstream: the values are compile-time constants
of the binary and the overlay computed them.

**Lag is computed at display time, not at build time.** The struct carries the
date the revision was pinned, and the page subtracts. A build that embedded
"days behind" would embed the build clock, and an identical source tree would
stop producing an identical binary.

**The page's product logo comes with the section.** Verified at the pinned
milestone: the version page is the only Android surface that serves the
pak-packed Chromium product logo (`IDR_PRODUCT_LOGO` /
`IDR_PRODUCT_LOGO_WHITE` in `version_ui.cc`'s Android block). The same edit
that adds the downstream section points those two resource ids at the TaffyGo
mark from `//taffy/resources/catalog`, so no Chromium logo renders on a
product surface (decision 0019's branding obligation). The other
`IDR_PRODUCT_LOGO_*` pak entries are unreferenced on Android; the release
lane's resource allowlist strips them, and dev builds carrying them unused is
accepted.

## Why the overlay cannot host it

The version page's data source, its handler and its template are all upstream
files, and there is no downstream registration hook for additional rows — no
observer, no delegate, no "extra fields" map that an embedder can populate.

The obvious alternative, a TaffyGo-owned WebUI at its own host, is not
cheaper: registering a WebUI host means editing
`ChromeWebUIControllerFactory`, which is the same class of upstream edit
against a file that changes more often. It is also worse for the person
reading it, who would have to know that a second version page exists.

## Rebase risk

**Medium.** The version page is refactored occasionally — it has moved between
handler shapes and template systems more than once — so this patch should be
expected to need real work at some rebases rather than a context fix. Two
things keep the cost bounded:

- all TaffyGo logic is behind one function call into the overlay, so a
  refactor moves the call site rather than rewriting the change;
- the eight rows are data, so a template migration is a mechanical rewrite of
  one loop.

**Retirement:** permanent. A Chromium derivative that cannot tell a person
which upstream revision it is has no answer to the first question a security
reviewer asks.

## Verify at SP-01

1. Whether the version page still has a C++ data source at the pin, or has
   moved to a Mojo interface. Read `chrome/browser/ui/webui/version/` and
   follow whichever shape exists.
2. Whether the Android version surface (the settings "About" screen) is a
   separate Java surface that needs its own rows. If it is, the same values
   reach it through `//taffy/app/android`, and this patch grows a
   fourth file — or the Java surface reads the JSON sidecar instead.
3. Whether a downstream section is better placed on the version page or on an
   internals page. The parity row says "build reports", so the release
   artifact manifest already satisfies half of it through the JSON sidecar
   that `write_upstream_provenance.py` emits; this patch is the half a person
   can see without a build server.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the three files in the checkout; commit as one logical change
./tools/chromium/export-patches
./tools/check fast
```
