# The BIP wire contract, as Mojo

**Status:** `[Proposed]` projection of the contract in
the protocol specification
and decision 0002
**Authority:** [taffy-core/contracts/bip/schema](../README.md).
This file is the Mojo projection of that schema and the schema wins on any
disagreement.
**Owning milestone:** M2 (page intelligence).

## What is here

Two files in one `taffy.mojom` module: `page_intelligence.mojom` holds the
`PageIntelligence` interface, the `PageDeltaClient` it calls back on, and their
message vocabulary; `page_intelligence_identity.mojom` holds the shared
identity, origin, lifecycle, and digest vocabulary imported by those messages.

## How the claim in its header is proved

The file's own header says it matches the schema "field for field". That is
checked, not asserted:

```bash
python3 taffy-core/contracts/bip/codegen/generate.py --mojom
```

It runs on any host, needs no Chromium checkout, and is part of the `contracts`
lane of `./tools/check fast`. It compares three things in order — the schema
against this file, then this file against the hand-written C++ enumerations in
`../public`, `../renderer` and `../browser` — and reports the first surface that
disagrees rather than reporting the same drift twice.

Two annotations in these files are read by that check, and both exist so an
exception is a visible contract edit rather than a line in a tool:

| Annotation | Means |
|---|---|
| `// bip-transport-only: <reason>` above a declaration | Mojo needs this message and the schema does not — a reply envelope, a request the JSON contract has no counterpart for |
| `// bip-not-projected: <Name> — <reason>` in the header block | The schema defines this and it never crosses the renderer boundary |

A declaration with neither, and no schema definition of its name, is a
finding. So is a schema definition with no projection and no entry. The C++
side has one matching annotation, `// bip-local-vocabulary:`, for the three
enumerations that share a wire name while meaning something narrower.

## Why the interface file remains past the soft line cap

`./tools/check fast` lane `files` applies the soft 400-line cap of
android-app-architecture section 5
to every non-generated source file. This one is over it and carries an entry
in `tools/check.d/file-size-exemptions.tsv` with a ceiling. The argument, in
full:

1. **It holds no logic.** The cap exists to bound how much a reader has to
   hold in their head to follow *behaviour*. A declaration list has none: no
   branch here can be wrong, only inconsistent with the schema — and that is
   gated separately by
   `python3 taffy-core/contracts/bip/codegen/generate.py --verify`, which decodes the
   golden and compatibility fixtures through this contract's own types.
2. **It is not an independent design.** Its structure is the schema's
   structure. The one vocabulary shared across every message — identity,
   origin, lifecycle, and digests — is split into the imported identity file;
   the remaining declarations change with the interface messages they form.
3. **The split adds no include burden.** Mojo's generated header for the main
   interface includes the generated header for its imported identity module,
   so existing consumers still include only `page_intelligence.mojom.h`.

The ceiling in the exemption table is deliberate: the entry permits the file
to be the size it is, not to keep growing. A schema change that pushes it past
the ceiling re-opens the question rather than passing silently.

## What this directory deliberately does not do

- **It imports no external vocabulary.** Not `mojo_base` time, not `url.mojom`.
  The main interface imports only this contract's identity file. The contract
  carries its own `Origin`, its own `UrlMetadata` and its own
  monotonic-millisecond time type, so the fixtures in `taffy-core/contracts/bip/compat`
  can be decoded through the generated types with no translation layer. See
  the reasoning in [`BUILD.gn`](BUILD.gn).
- **It generates no Java.** The Android UI does not talk BIP. BIP terminates at
  the browser-side page-intelligence adapter; Kotlin and Java consume the
  separate, profile-scoped Core API
  (decision 0039).
- **It decides nothing.** An interface definition grants no authority. What an
  action is allowed to do is `//taffy/browser`'s decision, per
  action.
