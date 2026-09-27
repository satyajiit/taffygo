# file-engine

**Status:** `[Proposed]` — this crate has no decision record of its own. Its
scope is the file-engine row of the implementation plan's M7 package table,
and that row is the authority on what it is for.
**Implementation status:** `[Current]` generation and structural validation
for all four formats exist, together with the source-bearing report seam above
them, and their suites run under `cargo test -p file-engine --locked`. That is
a host result. The crate compiles into the sandboxed core service, so a
Chromium build exercises it as code; no device evidence names this crate, and
the product route that would put a generated file in a person's hands is M7
work that has not happened.
**Owning work package:**
WP-M7-04
— the file engine. **Owning milestone:** M7.

## Authority boundary

This directory is the authority on **what a first-party XLSX, DOCX, PPTX or
PDF is, byte for byte**, and on whether given bytes are structurally that
thing. Both halves are deliberately one operation: `generate` produces bytes
and then validates its own complete output before returning it, and `validate`
is that same structural validator exposed separately for bytes crossing a
storage or process seam. A generator whose output nothing checked, and a
checker that had never seen what the generator emits, would be two ways to be
wrong about one format.

It is **not** the authority on what a document says. Content, ordering
authority and attribution come from the workspace snapshot;
`core-runtime`'s workspace export adapter projects one immutable revision into
the narrow sourced-report interface here, and a model chooses a registered
format rather than authoring bytes, archive members, formulas or an uncited
replacement document.

Determinism is the property everything else hangs from. Nothing here reads a
clock, a locale, a random source, a file, a network resource or an environment
variable. OOXML packages are written by a small ZIP-**store** implementation
with fixed timestamps and sorted paths, so the same report produces
byte-identical files even when its input vectors arrive in a different order —
which is what makes `tests/golden.rs` able to pin a length and a checksum at
all.

Bounds are the caller's. `Limits` is supplied per call, its defaults target an
interactive phone task rather than format maxima, and the implementation never
silently raises one: an exceeded bound is a named error and no partial file is
returned.

## What is here

| Module | Owns |
|---|---|
| `model.rs` | The typed specification a caller may express: workbook, document, deck, PDF, and the closed cell vocabulary |
| `limits.rs` | The explicit ceilings for one generation or validation |
| `zip.rs`, `xml.rs` | The deterministic package and markup writers the OOXML formats share |
| `ooxml/` | XLSX and PPTX; `docx.rs` sits beside them for the third |
| `pdf.rs` | PDF output, using only the built-in Helvetica font |
| `validate.rs` | The structural validator both entry points run |
| `report.rs` | The authoring seam above the four encoders: source-bearing research material, citation numbering, layout, formula-safe cells and a source index |
| `error.rs`, `sink.rs` | The closed failure vocabulary and the bounded output sink |

## What this crate deliberately does not do

- **It does not claim render fidelity.** What is proved is package structure,
  relationship locality, CRCs, XML well-formedness, PDF object offsets and the
  format-specific required parts. How a viewer paints the result is outside
  that claim, and nothing here should be read as a visual guarantee.
- **It has no formula input type.** A cell is text, a number, a boolean or
  blank. Text beginning with a formula sigil remains an inline string, so a
  value read off a page cannot become something a spreadsheet evaluates when
  the file is opened.
- **The ZIP writer stores and refuses.** No compression, no encryption, no
  data descriptor, no ZIP64 field, no duplicate path, no absolute path and no
  traversal. A package it cannot write exactly is a refusal, never an
  approximation.
- **No macros, executable content, arbitrary templates, charts, images, or
  PDF merge and split.** Each is absent because a caller could otherwise place
  bytes this crate does not understand inside a file it signs off as valid.
- **It writes nothing and opens nothing.** Bytes leave through the return
  value; custody is the caller's, and there is no path in any interface here.
- **It is not the sandboxed worker's path.** The registry's `document.build`
  and `spreadsheet.build` entrypoints belong to
  [`taffy-core/services/tool-runtime/python`](../../../../../services/tool-runtime/python/README.md),
  which is a separate process with a separate admission. This crate runs
  in-process inside the sandboxed core and starts nothing.
- **It depends on nothing.** Both dependency lists are empty. Every shipping
  crate has to clear Chromium's vendoring review, so a third-party dependency
  here would be a cost paid on a path where the standard library is enough.

## Commands

```bash
cargo test -p file-engine --locked
./tools/check fast --only rust
```

The four suites answer different questions: `golden.rs` pins the exact bytes
of reviewed specimens, `round_trip.rs` runs generation into validation,
`adversarial.rs` drives the refusals — formula sigils, exceeded bounds,
malformed packages — and `sourced_report.rs` covers the citation and ordering
behaviour of the report seam. GN is authoritative for what ships:
`//taffy/components/intelligence/core/rust/file-engine:file_engine`, visible
to `core-runtime` and to nothing else.
