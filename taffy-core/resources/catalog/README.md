# `//taffy/resources/catalog`

**Status:** `[Decided]` mechanism; the launch language set beyond English is
`[Open (OD-060)]`
**Implementation status:** `[Current]` the catalogues exist and the
host-runnable gate passes. grit has run on the Android catalogue inside the
product build: `taffy_android_strings_grit` is in the graph and
`out/<profile>/gen/taffy/resources/catalog/` holds its `.d`, `.d.stamp` and
`taffy_android_strings_grd.R.txt`. The pak catalogue has produced nothing yet —
`taffy_strings` is declared but no pak is repacked, because the repack half of
patch [0003](../../../chromium/patches/0003-pack-taffy-string-resources.md)
is unapplied
**Owning milestone:** M1, work package WP-M1-01 in the
implementation plan
**Authority:** PAR-L10N-001 in the
browser parity matrix
for the requirement; the
voice and naming guide and
brand for the words themselves

Every word a person reads in a TaffyGo-owned surface lives here. PAR-L10N-001
is an M1 **Required** row and its acceptance statement is two claims — all
strings externalized, and pseudo-localization tested — so this directory is
built around making both claims checkable rather than assertable.

## Two catalogues, split by who renders them

| Catalogue | Output | Read by |
|---|---|---|
| [`taffy_android_strings.grd`](taffy_android_strings.grd) | `values-<locale>/taffy_android_strings.xml` → `R.string.*` | Java and Kotlin |
| [`taffy_strings.grd`](taffy_strings.grd) | `taffy_strings_<locale>.pak` → `IDS_TAFFY_*` | Browser-process C++ |

The split follows the output grit has to emit, not the topic. No message
appears in both, and the gate proves it.

The message text lives in parts under [`strings/`](strings), one part per
seam:

| Part | Derived from |
|---|---|
| [`taffy_download_strings.grdp`](strings/taffy_download_strings.grdp) | `DownloadDecision` and `ExternalIntentDecision` in `//taffy/common/public/taffy_download_intent.h` |
| [`taffy_version_strings.grdp`](strings/taffy_version_strings.grdp) | `UpstreamProvenance` in `//taffy/resources/branding/taffy_upstream_provenance.h` |

That derivation is deliberate. Each catalogue is complete against a closed
browser-process seam rather than a hand-kept list of outcomes, because a
refusal the browser can return and cannot explain is a refusal a person
experiences as a bug. The
screen catalog decides layout and
this directory owns the shared browser-process words.

## The gate

```bash
python3 taffy-core/resources/catalog/tools/check_strings.py
python3 taffy-core/resources/catalog/tools/check_strings.py --self-test
python3 taffy-core/resources/catalog/tools/check_strings.py --verbose
```

It needs no grit, no Chromium checkout and no Android SDK, which is what lets
it run on every host on every change. It reads three sources and applies one
set of rules to all of them: the grit catalogues here, every Android
`values*/**.xml` resource anywhere in the overlay, and every Kotlin and Java
source in the overlay.

| Rule | What it proves |
|---|---|
| E1 | No string literal reaches a surface a person reads |
| C1 | Every part is carried by exactly one `.grd`, and every referenced part exists |
| C2 | Names are well formed, text is non-empty, and every translatable message has a description |
| C3 | No name is defined twice inside one resource namespace |
| P1 | The accented pseudo-locale round-trips back to the source exactly |
| P2 | The bidi pseudo-locale round-trips back to the source exactly |
| P3 | Every placeholder survives both transforms untouched |
| P4 | No Latin letter falls outside the accent map |

`--self-test` runs the transforms against inputs that break a naive
implementation — a bare placeholder, positional arguments, a message with no
letters — and asserts that the accent map is a bijection. A round-trip check
over the catalogue only proves the catalogue is clean if the transform is
doing something, so the self-test runs first, every time, even in the full
gate.

`--verbose` prints both pseudo-localized forms of every message, which is the
fastest way to read what a translator's longest plausible string will do to a
layout before any layout exists.

## Why pseudo-localization is checked here rather than only on a device

Grit can generate the `en-XA` and `ar-XB` pseudo-locales, and a device build
that shows them is the real test. But that test needs a Chromium checkout, an
Android device, and a human looking at a screen — so it happens rarely, and
the three defects pseudo-localization finds are all cheap to introduce and
expensive to find late:

- a string that was never externalized stays unaccented on screen;
- a layout that cannot survive a longer translation clips;
- a layout that hard-codes a left-to-right reading order mirrors wrong.

Only the first is provable without a screen, and this gate proves it on every
change. The other two are what the device pass is for, and
[`taffy_android_strings.grd`](taffy_android_strings.grd) carries the
`VERIFY AT SP-01` note naming the GN argument that enables them.

## What this directory deliberately does not do

- **It does not hold proper nouns.** `app_name` is in
  `//taffy/resources/branding`, because a name is not translated and it has
  to vary with the build configuration.
- **It does not hold upstream Chromium strings.** Every browser string
  TaffyGo does not own stays upstream and is translated upstream. A copy here
  would be a fork of a translation.
- **It does not decide surfaces.** Which screen shows which message is the
  screen catalog's decision.
- **It does not register itself with the resource bundle.** A pak is not
  loaded by being built; it is loaded by being listed in repack rules that
  belong to files Chromium owns. That edit is
  [`chromium/patches/0003-pack-taffy-string-resources.md`](../../../chromium/patches/0003-pack-taffy-string-resources.md).

## Known gap, named rather than hidden

The shell's download-notification strings live in
`//taffy/app/android/shell/java/res/values/taffy_download_notification_strings.xml`
rather than in a catalogue here. They are externalized, and the gate above
covers them — including both pseudo-locale round trips — so PAR-L10N-001 is
satisfied for them today. What they are missing is a translator-facing
description, which an Android string resource has no attribute for. (The
status island's five strings used to be the other instance of this gap; they
retired with patch 0009 and the island they labelled.)

Moving them into a `.grdp` here is the right end state and is a change to
`//taffy/app/android`, which has another owner. It is one file moved
and one `deps` edge, and it should happen the next time that directory is
open for another reason.
