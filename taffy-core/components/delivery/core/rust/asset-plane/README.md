# `asset-plane`

The portable delivery policy for TaffyGo's own artifacts: which of them a
device should have, what to do about the ones it does not, and what a transfer's
ending means. It performs no input or output — the browser process does that
and reports back — so the whole plane runs on a host, in a test, with a clock
the test supplies.

Authority: `docs/decisions/0045-taffy-assets-are-a-delivery-plane.md`.

## Adding an asset is one edit

Add a row to `catalog/source/assets.json` and run

```bash
python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_catalog.py --write
```

That is the whole change. No Rust, no C++, no Kotlin, no string, no screen. The
plane will plan it, the browser will fetch and verify it, and the Taffy Assets
surface will list it, because every question any of them asks is answered from
the row. `a_new_row_needs_no_code` in `src/catalog/mod.rs` is the test that
keeps this true, and it is written so that it would have to be changed — not
merely updated — if the property were ever lost.

A row of a **kind** that already exists costs nothing at all. A genuinely new
kind of artifact costs one enum member, one catalogued string naming it to a
person, and whatever consumes it.

## Publishing an asset

A row may exist before its bytes do. An `unpublished` variant names no path, no
length and no digest, and the plane refuses to fetch it with a reason a surface
can show. When the bytes are built and uploaded, the variant becomes
`published` and names all four facts. The generator refuses each half-state, so
a placeholder can never later be read as a pin.

## Why the catalog is compiled in

Every digest here is a constant of the build. A catalog served at run time would
be a service that can point a device at bytes the product was never tested
against; a compiled-in one means a compromised delivery origin can withhold
bytes or corrupt them, and both end in the same refusal. The cost is that
publishing a revision is a product change, which is the correct price.

## What is not here

Sockets, files, clocks and randomness. The browser supplies the monotonic time
and the jitter for the same reason the model router does: a backoff schedule
that cannot be reproduced is a schedule nobody can test.

Nothing here authorizes anything. An asset in the catalog is an artifact the
product may fetch, not a capability, not a permission and not a decision that a
tool may run.
