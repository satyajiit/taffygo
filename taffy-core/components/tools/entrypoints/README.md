# The frozen tool-entrypoint registry

**Status:** `[Proposed]` implementation of decision
0064,
with decisions
0007 and
0040.
**Implementation status:** `[Current]` the generator, both tables and the
refusal exist and are tested on a host; **no worker of any runtime reads this
table yet, because no Python worker exists.** The registry was built before the
interpreter on purpose — it is the part that bounds what the interpreter can
ever be asked for, and bounding it afterwards would mean building the
unbounded thing first.
**Owning milestone:** M7, the sandboxed Python runtime.
**Generated surface:** [the entrypoint table](generated/docs/tool-entrypoints.md).

## What this is

The complete list of things a sandboxed worker may ever be asked to do. Not the
list of things one is asked to do today, and not a list a caller contributes
to: the list.

Three entrypoints are registered, and one of them is refused. The refused one is
the point rather than an oversight — see [the rule that
refuses](#the-rule-that-refuses).

## One source, three artifacts

```text
source/entrypoints.json          the only place a row is written
tools/generate_entrypoints.py    --write | --check | --self-test
generated/cpp/tool_entrypoints.h            the browser's table
generated/docs/tool-entrypoints.md          the readable table
../core/rust/tool-entrypoints/src/generated.rs   the sandboxed core's table
```

Neither table is edited by hand. `--check` is what makes that stick, and it runs
in the `catalog` lane of `./tools/check fast` beside the other compiled-in
tables. `--self-test` breaks the source twenty-four ways and requires every rule
to fire, because a `--check` over a generator that checks nothing is a green
result that means nothing.

The one decision taken over the rows is in `tool_entrypoint_registry.cc` and
`registry.rs`, which a person wrote. A generated file holds data; it does not
hold judgement.

## What "frozen" means

Four things, and each is structural rather than remembered.

1. **First-party source, compiled in.** The rows are generated into a C++
   table and a Rust table and compiled. `source/entrypoints.json` is a build
   input; nothing in the product opens it.
2. **Never loaded from disk.** Neither the crate nor the C++ target has a
   reader, a parser, or a dependency that owns one. The crate's dependency list
   is empty; the C++ target's names `//base` and nothing else, for `base::span`
   over a row's ports — the pointer-and-count form it replaced is what
   Chromium's unsafe-buffer diagnostic exists to refuse. The registry cannot be
   pointed at a second table because it cannot be pointed at a first one.
3. **Never delivered.** An entrypoint is not an asset. The delivery plane
   fetches bytes an artifact catalog pins, and it has no row shape that could
   carry a name a worker would then answer to.
4. **Never supplied by a caller.** In Rust `Entrypoint` and `NativePath` have
   private fields and crate-private constructors, so the only values of those
   types that exist anywhere are the `static` ones the generator wrote — a
   caller may compare an identity and cannot mint one. **C++ has no equivalent
   of that and does not need it.** `ToolEntrypoint` is a public aggregate and
   any translation unit including the generated header can write one; what it
   cannot do is get that value anywhere it would be believed. The table is
   `inline constexpr`, `AdmitToolEntrypoint` takes a `std::string_view` and
   reads no table but that one, and every lookup returns a pointer into it. The
   day a function here starts taking a `const ToolEntrypoint&` from a caller,
   that stops being true and this property has to be rebuilt rather than
   assumed.

The source file itself is held to the same rule. Every key the document may use
is listed in the generator, and a key naming a location or a body of code —
`path`, `module`, `argv`, `url`, `script` and their kin — is refused even if
somebody adds it to that list. A row that could name code is a row that could
point at other code.

## The rule that refuses

A row may name a `native_alternative`. When it does, the entrypoint is
**refused** and the native owner is named instead.

Starting a sandboxed interpreter to do something the product already owns
natively buys nothing and costs a process, a budget, an opened resource and an
attack surface. `table.reshape` is the worked example: selecting, ordering,
grouping and pivoting rows is arithmetic over a bounded table, and the portable
core owns arithmetic.

The row stays in the registry rather than being left out, and that is
deliberate. An absent name reads as "not written yet"; a named one reads as
"considered, and answered somewhere else". Refusal by name is reviewable and
absence is not — the same reason decision
0044 has the media
worker refuse three operations by name rather than omitting them.

**A refusal against an `active` native owner still refuses.**
`core.table.reshape` is built into the portable Rust core, so a request for
`table.reshape` must use that bounded path rather than start a worker. The
registry says which layer owns a capability; its state records whether that
owner is now present without changing the worker's refusal.

## Two gates, deliberately not written in terms of each other

The sandboxed core asks this table before it proposes a job
(`core-runtime`'s `validate_python_entrypoint`). The browser asks the same
generated rows again before it dispatches one
(`IsValidCoreToolJob` in `//taffy/browser`). Neither calls the other.

That is the shape decision
0033
already uses for authority: the process that performs the consequential act
does not take the proposing process's word for whether it may. Here the
consequential act is starting a process at all.

The two projections cannot disagree about the rows, because one generator wrote
both from one source. They could disagree about whether each side asks, and the
tests on both sides assert the same three refusals for that reason.

## What is not here

- **No runtime column.** `RunSignedWasmTransform` carries an entrypoint too and
  is not held to this table. The rows describe what a Python worker may do;
  admitting them for a second runtime would be this registry claiming something
  it never said. The WASM family has no service and no registry of its own, and
  adding one is that milestone's work.
- **No module, no path, no code.** The identity is the whole binding. A worker
  resolves it against its own compiled-in dispatch, and a totality test between
  the two belongs in the change that builds the worker — writing a module name
  here today would be pinning something nobody can honour.
- **No milestone column.** Decision
  0054
  gates the assistant's *tool* surface by milestone. This registry is a layer
  below that and bounds a *worker*; a tool that is unavailable never reaches a
  job, so a second gate here would be a second place for the same answer to be
  wrong.

## Adding an entrypoint

One edit and one command:

1. add a row to `source/entrypoints.json`, in ascending identity order;
2. run `python3 taffy-core/components/tools/entrypoints/tools/generate_entrypoints.py --write`;
3. update the exact-vector tests on both sides — they name every row on
   purpose, so that a row cannot appear without a reader.

Step 3 failing is the intended signal, not a regression. A new row widens what
a sandboxed interpreter may be asked for, and **removing a `native_alternative`
widens it further**: that edit moves a capability out of the product's own code
and into a sandbox, which is why `OWNERS` routes this directory's gate through
the security owners.
