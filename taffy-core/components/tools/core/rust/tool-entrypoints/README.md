# tool-entrypoints — the sandboxed core's copy of the frozen registry

**Status:** `[Proposed]` implementation of decision
0064,
with decisions
0007
and
0040.
**Implementation status:** `[Current]` the generated table, the types and the
refusal exist, and `cargo test -p tool-entrypoints --locked` runs the suite
that pins them on a host.
**Owning work package:**
WP-M7-03
— tool entrypoints. **Owning milestone:** M7.

## Authority boundary

**This crate is not the authority on the rows.**
[`taffy-core/components/tools/entrypoints`](../../../entrypoints/README.md) is:
it holds `source/entrypoints.json`, the generator, the readable table and the
browser's C++ projection, and it is where a row is added, argued and reviewed.
Read that page for what "frozen" means, for the native-alternative rule and for
the procedure that adds an entrypoint. Repeating any of it here would give one
subject two authorities, and the copy would be the one that goes stale.

What this crate owns is one question, asked from one process: **given a
string, may a sandboxed worker be started for it?** `src/generated.rs` is the
table this build was compiled with, and `src/registry.rs` is what a person
wrote about those rows — the types, and the decision `admit` makes over them.
It is the sandboxed core's gate. The browser has its own, over the same
generated rows in C++, and neither calls the other, because the process that
performs the consequential act does not take the proposing process's word for
whether it may. Here the consequential act is starting a process at all.

Two structural facts hold that boundary in place. The dependency list is
**empty** — no serializer, no reader, nothing that owns one — so this crate
cannot be pointed at a second registry because it cannot be pointed at a
first. And `Entrypoint`, `NativePath` and `Port` have private fields with
crate-private constructors, so the only values of those types anywhere are the
`static` ones the generator wrote: a caller may compare an identity and may
never mint one.

GN visibility names `core-runtime` and nothing else. No first-party Rust is
linked into the browser process, which is exactly why the browser reads the
generated C++ table beside this one rather than calling here.

## What is here

| File | What it is |
|---|---|
| `src/generated.rs` | The compiled-in rows, plus `REGISTRY_VERSION` and a `REGISTRY_FINGERPRINT` so one registry can be told from another. Generated; never edited by hand |
| `src/registry.rs` | The types the rows inhabit and the one decision made over them: `admit` answers admitted, refused-for-a-native-path with that owner named, or unknown |
| `tests/frozen_registry.rs` | The three separate promises: the refusal fires and names its owner, the surface is exactly the compiled-in vector rather than a count, and nothing can be added at run time |

`mod generated;` carries `#[rustfmt::skip]`, and it has to. Without it
`cargo fmt --all` rewraps the table the generator emitted, the generator's
`--check` then fails on a tree nobody edited, and the failure names the
generator rather than the formatter — so it reads as a stale table instead of
a formatting fight between two lanes.

## What this crate deliberately does not do

- **It adds no row and edits none.** A row is added next door, in the source
  document, and arrives here through the generator. This directory's copy is
  an output.
- **It opens nothing, reads no clock and holds no state.** One question about
  one string, which is why the core and the browser can both ask it and get
  the same answer without sharing anything else.
- **It does not run anything.** Admission is not execution: the worker that
  answers an admitted entrypoint is
  [`taffy-core/services/tool-runtime/python`](../../../../../services/tool-runtime/python/README.md),
  and it applies its own admission before it starts an interpreter.
- **It does not soften a refusal when the native owner ships.** `NativeState`
  records whether that owner is built today and changes no verdict. The
  registry says which layer owns a capability, not which layer has finished
  it — admitting an interpreter in the meantime would make "temporary" the
  reason the sandbox was widened.
- **It knows no module, path, argv or code.** The identity is the whole
  binding; a worker resolves it against its own compiled-in dispatch.

## Commands

```bash
cargo test -p tool-entrypoints --locked
python3 taffy-core/components/tools/entrypoints/tools/generate_entrypoints.py --check
./tools/check fast --only rust,catalog
```

The generator's `--check` is what keeps this file an output, and it runs in
the `catalog` lane. GN is authoritative for what ships:
`//taffy/components/tools/core/rust/tool-entrypoints:tool_entrypoints`.
