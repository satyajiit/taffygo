# 0029 — Register the Taffy core SQL metric

**Status:** `[Current]` — exported from the pinned Chromium checkout
**Needed by:** The profile-owned core journal in decision 0037
**Estimated size:** one upstream line in one metrics registry

## Upstream file and symbol

| | |
|---|---|
| File | `//tools/metrics/histograms/metadata/sql/histograms.xml` |
| Symbol | `<variants name="DatabaseTag">` |

## The change

Add `TaffyCore` to Chromium's closed SQL database-tag registry. The browser
storage broker then constructs its physical writer with
`sql::Database::Tag("TaffyCore")`, so SQLite errors and timings are attributed
to the product journal instead of another subsystem.

## Why the overlay cannot host it

`sql::Database::Tag` validates its literal at compile time against the
upstream metrics variants. There is no honest generic production tag and
borrowing another component's identity would make its telemetry false. The
database implementation, schema, and writer remain entirely under `//taffy`;
only the closed metric-name registry needs this upstream edit.

## Rebase risk

**Low.** The edit is one sorted variant. A registry rename or removal fails at
the `sql::Database` construction site during compilation.

## Retirement

Permanent while the browser owns this SQLite journal; reviewed at each
Chromium milestone rebase. If Chromium replaces the closed tag registry, move
the same distinct product identity to its supported replacement.

## Verification

Export from `taffy/patched` with `Taffy-Patch: 0029`, regenerate the SQL name
variants, and build `//taffy/components/storage/browser:storage` plus
`//taffy/browser:core_service_client` in the pinned checkout.
