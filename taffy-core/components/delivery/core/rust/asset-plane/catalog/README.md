# The asset catalog

`source/assets.json` is the source of truth. `../src/catalog/generated.rs` is
generated from it by `tools/generate_catalog.py` and is never edited by hand.

```bash
# after editing source/assets.json
python3 taffy-core/components/delivery/core/rust/asset-plane/catalog/tools/generate_catalog.py --write
```

The `catalog` lane of `./tools/check fast` runs `--check` and `--self-test`:
the first fails on a stale table, the second breaks the source and a model-row
fixture in every way the rules name — the count is printed, so a rule that
stopped being checked shows up as a smaller number — and fails if any of the
rules that should have caught one did not.

`CATALOG_FINGERPRINT` is derived from the rows rather than declared beside
them, so it cannot be forgotten in a review and cannot disagree with what it
names. Reordering rows does not change it; changing a revision does.

## A model row

A row of kind `model-weights` or `model-tokenizer` is held to two rules the
other kinds are not, both in `tools/model_rows.py` and both run by the same
`--check` and `--self-test` above. There are no rows of either kind today.

1. **It says what the artifact is, not only what it is for.** A `model` block
   names a `format` (`litert-tflite`, `onnx-runtime` or `gguf`) and a `role`
   (`whole` or `adapter`). Those are two of the four facts a sandboxed worker
   needs; the byte length and the digest are every row's already. Nothing on a
   device can derive either one, and the block is refused on any other kind.
2. **It names where the bytes came from and under what terms.** `upstream_url`
   is the https URL they were taken from, and `licence_record` names a
   directory under `taffy-core/third_party/` whose `README.chromium` fills
   URL, License and License File — with that licence file in the tree.

The second is a hard precondition rather than a convention, because the check
that normally catches an unrecorded binary cannot see one of these: the `files`
lane sweeps shipping binaries from disk, and a delivery-plane artifact is never
on this disk. The row is the only thing in the tree that names it.

Decision
0101
consumes the `model` block through a bounded complete Core State snapshot. The
browser hashes the exact installed revision and registers only matching facts;
no model row is published until its vendor, licence and runtime facts exist.
