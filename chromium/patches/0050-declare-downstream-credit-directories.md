# 0050 — Declare downstream credit directories

**Status:** `[Current]` — exported; product rebuild and packaged-credit verification pending
**Needed by:** CommonMark Java core and GFM tables shipped in the Android assistant
**Estimated size:** 4 added upstream lines, 1 file

## Upstream file and symbol

`//components/resources/BUILD.gn`, the `declare_args` block beside
`generate_about_credits`.

## The change

Declare `extra_third_party_notice_dirs` as an empty list. The existing
`about_credits` action already forwards this value to Chromium's license
generator when it is defined, but no declaration makes a downstream GN
argument visible to that action. The empty default preserves upstream notice
discovery; Android profiles import one overlay-owned fragment that supplies
the CommonMark directory and keeps credit generation enabled.

The product mount is a symlink, which the default filesystem notice scan does
not follow. Explicit discovery loads both shipped entries from CommonMark's
`README.chromium`, their complete BSD license, and their dependency-file inputs.
No license text is copied into first-party code or summarized in the notice.
The website publication required by decision
0127
remains a separate action.

## Why the overlay cannot host it

The APK consumes the upstream `about_credits` action. An argument assigned only
in the overlay is not visible without a matching declaration in a scope the
action reads. Rewriting generated HTML would last only until that action ran
again. Declaring its existing downstream hook is the smallest durable change;
the directory selection stays in `//taffy/build/args/credits.gni`.

## Rebase risk and retirement

**Low.** Android owns this patch. Retire it when upstream declares the existing
hook or provides an equivalent downstream notice input. Review the declaration
and forwarding action at every milestone rebase. The existing patch-budget
findings remain open; this entry does not raise their limits.

## Export and verification

The one-file edit was committed with `Taffy-Patch: 0050` and exported by
`./tools/chromium/export-patches` on 2026-09-09. The retained
`ui-revamp-patch-export.log` records exit 0 and 46 exported queue files.
Standalone notice generation with the exact extra-directory input contains
both CommonMark names and two full copies of their committed BSD license;
`commonmark-credits-content-check.log` records those checks. These logs are
under `.taffy/evidence/agent-loop-20260909/` and are local evidence, not shipped
artifacts. Rebuild the Android product and check that
the generated `about_credits.html` contains both CommonMark entries and the
complete committed license. The three Android profiles must all import the
shared credits fragment. No website publication or release acceptance follows
from that build check.
