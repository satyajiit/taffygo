# EasyList — the base filter-list snapshots and pack recipe

This directory holds the pinned EasyList and EasyPrivacy snapshots the
filtering plane's base rule pack is built from, the licence they travel
under, and the recipe that packages them. Decision
0076
chose the lists and resolves OD-070; this directory is that choice made
buildable.

| Fact | Value |
|---|---|
| EasyList version | 202608272347 (upstream commit `7d32b4e454fcfd143af380ae2132bb418e95c719`) |
| EasyPrivacy version | 202608272349 (upstream commit `db6ba1a88fd55fc863b851e9f7310a98e68aafa3`) |
| Snapshot date | 2026-08-27 (UTC), from easylist.to |
| Licence | GPL-3.0, elected from the authors' GPL-3.0 / CC BY-SA 3.0 dual offer |
| Pack sha256 | `c1ea235b7eb1de2efa7b6fd6f5af802fa548e779b9e008efe98ff629a16eef10` |
| Pack length | 1,183,665 bytes |

## Why the snapshots are committed

The flag-icons recipe next door commits no artwork, because its input is an
immutable, digest-addressable npm tarball anyone can fetch again. EasyList
has no such object: easylist.to serves only the current list and expires it
in days, so a digest pin with no bytes behind it would stop being
reproducible the day upstream rolls. The committed snapshots are the durable
pin; `tools/manifest.json` records the digests they arrived with, and
`tools/build_filter_pack.py` refuses to package bytes that differ.

## The licence election

The EasyList authors offer the lists under GPL-3.0 **or** CC BY-SA 3.0
(https://easylist.to/pages/licence.html). This vendoring elects GPL-3.0. The
lists are data the browser reads — compiled into a matcher's index at load,
never linked as code — so the election obliges exactly what this directory
does: carry the terms with the work. The text is
`vendor/easylist.GPL3.txt`, and it travels inside the pack as its own
`LICENSE` member, so a device that has the rules has the terms beside them
without owing any screen an attribution surface. CC BY-SA is the road not
taken for the same reason the flag corpus refused it: it earns an
attribution surface no screen owes.

## Building and publishing

```bash
python3 taffy-core/third_party/easylist/tools/build_filter_pack.py --output /tmp/easylist-base.zip
```

The builder verifies both snapshots against their pins, writes the one
deterministic zip shape `artifact_zip.py` produces, and prints the exact
published-variant fragment the catalog row takes. Publication is
`./tools/assets` (upload, then verify the origin serves the same bytes
back); a variant of the catalog row in
`taffy-core/components/delivery/core/rust/asset-plane/catalog/source/assets.json`
stays `unpublished` until that has happened for it. The two Android variants
were published on 2026-09-02: the pack is at
`filter-lists/202608272347-taffy.1/easylist-base.zip` under the delivery
origin, and the verification report's section 2.6 holds the command and what
it printed. The desktop variants remain unpublished. A refreshed list pair is a
new snapshot pair, a new revision and a new row — never an edit to this one.

The product APK still carries the same pinned snapshots as one uncompressed
asset (`assets/taffy-filter-lists/base.txt`), built by `--concat`, so Ads and
trackers has rules on first launch, before the pack has arrived and on a
device with no published variant. The catalog pack wins when it is installed:
the pack is a required part, so a profile fetches it on its own on any live
connection, and the browser reloads the ruleset the moment it lands.

`--self-test` proves the builder's own properties (pinned-digest refusal,
byte-identical rebuild, exact member set) and runs in the `catalog` lane of
`./tools/check fast`.
