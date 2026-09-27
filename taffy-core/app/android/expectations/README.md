# Android lint expectations

**Status:** `[Current]` `lint-baseline.xml` records only findings produced by
the direct product target `//taffy/app/android:taffy_public_apk`.

The baseline is evidence, not an allowlist. Regenerate it only from that target
in a mounted Chromium checkout, inspect every addition, and keep generated
paths rooted under `gen/taffy/`. A source finding under `//taffy` is a defect and
must not be hidden here.

The product target also uses Chromium's shared lint suppressions for upstream
libraries. Taffy-owned source does not add a broad suppression file.
