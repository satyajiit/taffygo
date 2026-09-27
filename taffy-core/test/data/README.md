# `//taffy/test/data`

**Status:** `[Current]` — a mount point, not a data directory. Nothing is
committed here and nothing should be.

**Owning work packages:** WP-M1-06 and WP-M2-08 in the
implementation plan.

## What is expected here

Four paths, all mounted from this repository by the fork tooling rather than
copied:

| Path | Mounted from | Read by |
|---|---|---|
| `web/` | [`test-fixtures/web/`](../../../test-fixtures/web/README.md) | every suite in `//taffy/test`, through `corpus/corpus_mount.h` |
| `tasks/` | [`test-fixtures/tasks/`](../../../test-fixtures/tasks/README.md) | the task benchmark adapters in `//taffy/test` |
| `contract/golden/` | [`taffy-core/contracts/bip/golden/`](../../contracts/bip/README.md) | the fuzzer seed generator in `fuzz/` |
| `contract/compat/` | `taffy-core/contracts/bip/compat/` | the same generator |

`./tools/chromium/sync` symlink-mounts `taffy-core` at `src/taffy` and mounts
these four fixture authorities below it. Until they
have it every read fails loudly with the path it expected — see
`corpus/corpus_mount.h` for why that is a failure rather than a skip.

## Why the corpus is not copied

The corpus is versioned and immutable: fixtures, expected observations,
scripted model outputs, expected audit events and expected outputs share one
version, and changing any of them is a version bump plus a like-for-like
comparison run plus benchmark-owner approval. A copy inside the Chromium
checkout would be a second corpus that could drift from the first, and the
drift would be invisible — every test would keep passing against the copy while
the benchmark ran against the original.

## What to do when a suite fails at the mount

The message names the absolute path it tried. Mount the tree there. Do not
weaken the check, and do not turn the failure into a skip: a suite that passed
because it could not find the pages or the seeded secrets it was supposed to
read would appear in a milestone exit-evidence packet as a green row.

## Related

- [`../README.md`](../README.md) — what this directory's suites are and what
  they deliberately do not do
- `../TEST-INDEX.md` — every suite, the requirement it
  proves, and where it runs
- Benchmark corpus — the
  authority for the fixture list and the versioning rule
