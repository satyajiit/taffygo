# adblock-rust — the vendored matching engine

**This directory holds third-party source. TaffyGo did not write the engine
and does not maintain it.** `vendor/` is Brave's `adblock-rust` and the
crates.io dependencies `cargo vendor` resolved for it, each under the terms it
arrived with. First-party code here is glue and nothing else: the C ABI in
`c_abi/`, the generator in `tools/`, and the `BUILD.gn` that generator writes.
Nothing under `vendor/` is TaffyGo's work, and the repository's own `LICENSE`
does not reach it.

**Status:** `[Proposed]` — decision
0086
chose this engine and is accepted at the M4 exit review with the on-device
numbers. It supersedes decision
0077.
**Implementation status:** `[Current]` the engine, the C ABI and the generated
build graph exist, and the filtering plane's three `unit_tests` targets inside
`taffy_unittests` are the verification of record for them. A phone fetching
the published rule pack is a separate, absent result — the implementation plan
row says which half is which.
**Owning work package:**
WP-M4-01
— the filtering plane, which also owns
[`taffy-core/components/filtering`](../../components/filtering/README.md) and
[`taffy-core/third_party/easylist`](../easylist/README.md).
**Owning milestone:** M4.

## Authority boundary

Four documents each own part of this, and this page owns the last one only.

- **`README.chromium` is the provenance record.** It is the authority on what
  is vendored, at what version, under which licence, and with which local
  modifications. Read it before changing anything here. This page does not
  copy its fields, because a second copy of a provenance record is a licensing
  defect waiting to happen rather than a convenience.
- **`vendor/adblock.txt` is the vendored-asset record** the `files` lane holds
  to the bytes: `tools/lib/vendored_assets.py` names it, the directory it
  covers, and the MPL-2.0 text beside the engine's sources.
- **Decision 0086 is the authority on why this engine**, and on the product
  rules above it — one strictness, no acceptable-ads concept, free for every
  account, and no vendor branding anywhere a person can see. The user-facing
  name is **Ads and trackers**; the engine is an implementation detail.
- **This page is the authority on one thing: the boundary the engine is
  reached across**, and what may and may not cross it.

## The boundary

**A C ABI, in the browser process, with one caller.** A subresource request
exists only in the browser process and never reaches the sandboxed core, so
the verdict has to be answerable there. TaffyGo's own Cargo crates are not
linked into that process — and this crate is not one of them. It is not a
member of the Cargo workspace, `cargo test --workspace` does not walk it, and
no host command in this repository compiles it. GN does, as a
`rust_static_library` behind `source_set("c_abi")`, whose visibility names
`//taffy/components/filtering/core:*` and nothing else.

**Ownership of returned memory belongs to the ABI.** Strings and byte buffers
that cross the header are freed by `taffy_adblock_string_free` and
`taffy_adblock_bytes_free`, never by Chromium's allocator; the filtering
plane's `scoped_adblock_string.h` is what makes that structural on the calling
side rather than remembered.

**The `BUILD.gn` is generated.** `tools/generate_crate_gn.py` walks
`vendor/*/Cargo.toml`, resolves features from the `adblock` crate, and emits
one `cargo_crate()` target per enabled crate plus the two first-party targets.
It does not invent a second dependency graph, and the file it writes carries
an `@generated` banner. **No lane of `./tools/check fast` runs it** — the
`files` lane checks the provenance record, not the build graph — so a vendored
change is finished when the generator has been re-run and the tree builds,
never when the fast lane is green.

## What is here

| Path | What it is | Who wrote it |
|---|---|---|
| `vendor/adblock-v0_13/` | The matching engine, with its own `LICENSE` (MPL-2.0) | Third party |
| `vendor/*` | The transitive crates `cargo vendor` resolved, each keeping the terms it arrived with, beside its own sources | Third parties |
| `vendor/adblock.txt` | The vendored-asset provenance record the `files` lane reads | First-party record of third-party assets |
| `README.chromium` | The provenance record, in Chromium's own format | First party |
| `c_abi/include/taffy_adblock.h`, `c_abi/src/lib.rs` | The C ABI: create an engine from list text or from a serialized form, match one request's facts, ask the cosmetic questions, destroy it | First party |
| `tools/generate_crate_gn.py` | The build-graph generator | First party |
| `BUILD.gn` | Generated. Do not edit | Generated |
| `OWNERS` | Routes a vendoring change through the owners a vendoring change needs | First party |

## What this directory does not do

- **It holds no filter lists.** The EasyList family is pinned in
  [`taffy-core/third_party/easylist`](../easylist/README.md) and delivered as
  an asset; nothing here has an opinion about which rules a person gets.
- **It holds no product behaviour.** The request-path throttle, the posture,
  the counters, the week window and the per-site exception are the filtering
  plane's, one directory over. This is a matcher.
- **It touches no task fact.** A blocked request never enters the journal, the
  Core API or the AI data plane.
- **It does not restate or weaken an inbound licence.** MPL-2.0 covers the
  engine; every transitive crate keeps its own terms next to its sources.
  Those are honoured separately and unchanged, and nothing about TaffyGo's own
  proprietary licence applies to them.
- **It is not modified freely.** `README.chromium`'s "Local Modifications"
  section is the complete record of every divergence from upstream, and an
  edit under `vendor/` that is not written down there is invisible at the next
  version bump — which is when it will be silently reverted.

## Changing what is vendored

A change that adds, removes or replaces a vendored crate is a **vendoring
review, not a build review** (`OWNERS`). In order:

1. replace the sources under `vendor/`, keeping each crate's licence file
   beside them;
2. update `README.chromium` — version, licence, and every local modification;
3. update `vendor/adblock.txt` if the engine's version or terms moved;
4. regenerate the build graph:

```bash
python3 taffy-core/third_party/adblock-rust/tools/generate_crate_gn.py --write
python3 taffy-core/third_party/adblock-rust/tools/generate_crate_gn.py --check
./tools/check fast --only files
```

5. build and run the filtering targets, because nothing on a host compiles
   this directory:

```bash
./tools/chromium/build --profile dev-arm64 taffy_unittests
./tools/chromium/test --profile dev-arm64 taffy_unittests
```
