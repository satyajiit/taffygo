# table-engine

**Status:** `[Proposed]` — this crate has no decision record of its own. Its
scope is the table-engine row of the implementation plan's M7 package table,
and it is also the native owner decision
0064
names when it refuses `table.reshape` to a sandboxed interpreter.
**Implementation status:** `[Current]` parsing, exact decimal arithmetic, the
seven closed steps, lineage accounting and spreadsheet-safe export all exist,
and their suites run under `cargo test -p table-engine --locked`. That is a
host result over pure functions; the crate compiles into the sandboxed core
service, and no device evidence names it.
**Owning work package:**
WP-M7-05
— the table engine. **Owning milestone:** M7.

## Authority boundary

This directory is the authority on **what reshaping one bounded table means**:
which operations exist, what each does to rows and cells, how numbers compare
and combine, and which comma-separated bytes come out. The vocabulary is
closed — `Select`, `Order`, `Filter`, `Group`, `Pivot`, `Dedupe`, `Limit` —
and a recipe is either one of those sequences or a named refusal. There is no
expression language, no join, no user-defined function and nothing a caller
can extend at run time.

It is **not** the authority on whether a reshape may happen. That is decided
above: `policy-engine` decides, the registry next door decides that this work
belongs to the core rather than to a worker, and
[`loop-kernel`](../loop-kernel/README.md)'s `native_table` adapter is the one
caller — its GN visibility names that crate and nothing else. This crate is
handed two bounded operands and answers with a table or an error.

Everything is pure. No clock, file, network resource, environment variable or
random source is read, and cancellation is cooperative: a caller polls its own
source and gets no partial table when it stops.

Two properties are worth reading before editing anything here.

**No binary floating point enters a table.** `Decimal` is a normalized exact
base-ten value, so a sum of currency figures is the sum a person would write
down rather than the nearest representable double. Ordering, filtering and
aggregation all compare through it, which is also why `Sum` is accepted only
in decimal comparison mode.

**A cell remembers where it came from.** `SourcedCell` carries the ordered,
de-duplicated input cells that contributed to it, bounded per cell and in
total. That lineage is what lets a result be attributed rather than asserted,
and it is why the lineage bounds are limits like any other rather than an
implementation detail.

## What is here

| Module | Owns |
|---|---|
| `csv.rs` | Strict RFC 4180 parsing, and the export half: quoting, line endings, and the spreadsheet hardening below |
| `decimal.rs` | The exact base-ten value every comparison and aggregate goes through |
| `json.rs`, `recipe.rs` | The strict reader for a recipe document and the closed typed vocabulary it decodes into, with every bound checked at construction |
| `transform/` | The steps themselves, applied in the recipe's own order, with the output bound and the lineage accounting they share |
| `model.rs` | `Table`, `SourcedTable`, `SourcedCell`, and the `CellRef` that lineage is written in terms of |
| `limits.rs`, `error.rs` | The ceilings and the closed failure vocabulary |

## What this crate deliberately does not do

- **It repairs nothing.** A lone line break, a quote in an unquoted field,
  bytes after a closing quote, an unterminated quoted field, a duplicate or
  empty header, a ragged row, a disallowed control character, a NUL and
  invalid UTF-8 are each their own named refusal. A parser that guessed would
  turn a malformed input into a confidently wrong table.
- **It evaluates no formula, and it neutralizes one on the way out.** Export
  prefixes a formula-shaped cell — one starting `=`, `+`, `-` or `@`, leading
  whitespace included — with an apostrophe, while leaving a signed number
  such as `-10.50` untouched. The bytes a spreadsheet opens are data, never a
  reference to another cell.
- **It returns nothing partial.** Every exceeded bound and every cancellation
  is an error with no table attached, because a truncated table reads exactly
  like a complete one.
- **It reads and writes nothing.** No path, no handle and no I/O of any kind
  appears in any interface here.
- **It is not a spreadsheet or a file format.** Producing an XLSX, DOCX, PPTX
  or PDF is [`file-engine`](../file-engine/README.md)'s; this crate stops at
  rows.
- **It depends on nothing.** The dependency list is empty, for the same
  vendoring-review reason the rest of the shipping Rust keeps its tree near
  zero.

## Commands

```bash
cargo test -p table-engine --locked
./tools/check fast --only rust
```

`tests/reshape/bounds.rs` drives the ceilings and `tests/reshape/security.rs`
drives the refusals and the export hardening. GN is authoritative for what
ships: `//taffy/components/intelligence/core/rust/table-engine:table_engine`.
