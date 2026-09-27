# flag-icons — the country-flag pack

This directory holds the recipe that builds TaffyGo's country-flag artifact. It
holds no artwork. The upstream tarball is fetched by checksum at build time, the
pack is built from it, and the bytes are published to the delivery plane — so
what is committed here is the specification of an artifact, not the artifact.

`tools/build_flag_pack.py --self-test` and `tools/svg_raster.py --self-test`
both run on any host with the pinned rasteriser present and report what they
checked.

## Provenance

| Field | Value |
|---|---|
| Upstream | `flag-icons`, https://github.com/lipis/flag-icons |
| Version | 7.5.0 |
| Distribution | `https://registry.npmjs.org/flag-icons/-/flag-icons-7.5.0.tgz` |
| Tarball sha256 | `c0b80bf0e08006a60f56621d6bc49f8c7131f4d1fef6737a165a673431f4b518` |
| npm shasum | `66462df3e8bc5ef3a283322a46bb9dde1b290505` |
| Copyright | Copyright (c) 2013 Panayiotis Lipiridis |
| Licence | MIT, carried verbatim as the pack's own `LICENSE` member |
| Licence source | `package/LICENSE` inside the pinned tarball |
| Set used | `flags/4x3` only. The `1x1` set is not built |
| Rasteriser | librsvg 2.58.0 with cairo 1.18.0, both refused on mismatch |
| Encoder | `cwebp` 1.3.2, `-lossless -exact -z 9 -metadata none` |
| Raster | 192 × 144, one size for every density |
| Container | `zip`, `taffy-core/third_party/cpython/tools/artifact_zip.py` shape |
| Members | 249 flags at `flags/<iso>.webp`, plus `LICENSE` |
| Pack sha256 | `46d06699bd17d6943eeb2d4540b707eae34c6313ac01d1835c61201b2decb5b5` |
| Pack length | 584,892 bytes |

The digest and the length are reproduced by rebuilding: two builds from the same
tarball on the same pinned pair produced byte-identical output. They are the
facts the compiled-in catalog pins, which is what makes a corrupted or
substituted download a refusal rather than a wrong picture.

## What the licence does and does not cover

MIT covers the artwork as distributed and carries one obligation: the copyright
notice and the permission notice travel with the copies. They do, inside the
pack, so a device that has the flags has the licence beside them and no screen
owes an attribution surface. That is the whole reason this corpus was chosen
over twemoji, which is CC-BY and would have required one.

A flag is not a trademark and no vendor permission is implied or needed — this
is unlike `taffy-core/ui/android/core/ui/vendor/provider-marks.txt`, where the
distribution's licence covers the SVG files and cannot license anybody's mark.

## Which countries are in it, and who decided

Exactly the 249 entries of ISO 3166-1 alpha-2, and nothing else. The list is
committed at `tools/iso-country-codes.tsv` and the build reconciles it against
the upstream set in both directions, so a code with no artwork and a flag with
no code are each a failure rather than a silent omission.

Two independent sources agree on that set: flag-icons' own `iso: true` metadata
and the JDK's `Locale.getISOCountries()`. That agreement is what lets the pack
be rebuilt on a host with no JDK.

Decision `docs/decisions/0047-the-country-flag-corpus-is-iso-3166-1.md` records
why the standard is the answer to the contested cases rather than a position
taken case by case. Read it before adding or removing a code here.
