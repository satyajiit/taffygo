# 0011 — Parse Kotlin `enum class` in compile_java.py's type check

**Status:** `[Current]` — applied 2026-08-17 as part of the first ARM64
product build
**Needed by:** WP-M1-01 — every Kotlin enumeration in the overlay fails the
build without it
**Estimated size:** 1 modified upstream line (plus a comment), 1 file

## Upstream file and symbol

| | |
|---|---|
| File | `//build/android/gyp/compile_java.py` |
| Symbol | `_TOP_LEVEL_CLASSES_RE` |

## The change

The regular expression that extracts a source file's top-level type name knows
Java's `enum Foo` but not Kotlin's `enum class Foo`: the `enum` alternative
matches the keyword and the capture group then takes the word `class` as the
type name, so the package/path consistency check fails every Kotlin
enumeration with

```text
Source package+class name do not match its path.
Expected path: org/chromium/taffy/common/class.kt
```

Three defects in the same expression, fixed together because they are one
statement — "the parser does not know Kotlin":

1. **`enum class`.** The keyword alternative gains an optional group,
   `enum(?:\s+class)?`, so Kotlin's `enum class Foo` yields `Foo` instead of
   `class`.
2. **`object`.** Kotlin's singleton declaration is a file's public identity
   the same way a class is; the keyword list gains `object`, so a file whose
   public type is an object is checked against that name rather than against
   whatever private helper follows it.
3. **`private`.** Removed from the matched modifiers: a private top-level
   type is legal only in Kotlin and is never the file's public identity, and
   matching it misnamed files whose public type follows a private helper.
   Java has no private top-level types, so nothing changes for Java.

```python
r'(?:(?:public|protected)\s+)?'
r'(?:(?:static|abstract|final|sealed)\s+)*'
r'(?:class|@?interface|enum(?:\s+class)?|record|object)\s+'
```

Every previously matched Java shape keeps matching; indented (nested)
declarations stay unmatched, as the expression intends.

## Why the overlay cannot host it

The expression lives in Chromium's Java/Kotlin compile wrapper, which every
`android_library` runs. There is no per-target hook to replace the parser,
and the alternative — renaming product enumerations to satisfy a parser bug —
would let a build tool dictate the shape of product code.

Upstream has not hit this because its Kotlin allowlist admits four paths and
none of them contains a top-level `enum class`. TaffyGo's overlay contains
several.

**Upstreamable.** This is a plain defect fix with test coverage value for
upstream's own Kotlin adoption; it should be proposed upstream at the next
opportunity, which is the one path by which this patch retires early.

## Rebase risk

**Low.** One line in a slow-moving build tool. If upstream rewrites the
parser, the patch conflicts loudly and the rebase re-answers whether the new
parser handles Kotlin enumerations — which is the right question to be forced
to ask.

**Retirement:** when upstream takes the fix, or when the parser learns Kotlin
properly.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit build/android/gyp/compile_java.py in the checkout; commit with the
# Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```
