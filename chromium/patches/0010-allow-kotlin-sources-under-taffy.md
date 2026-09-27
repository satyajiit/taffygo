# 0010 — Allow Kotlin sources under `//taffy`

**Status:** `[Current]` written and exported. This is the first patch in the
queue that exists as a diff rather than as an argument, and the first one found
by a build rather than by reading
**Needed by:** WP-M1-04 (the Compose island), and before it every
`android_library` target in `//taffy/app/android` — nine of them carry
Kotlin
**Realised size:** 6 modified upstream lines, 1 file (1 functional line, 5 of
comment)

## Upstream file and symbol

| | |
|---|---|
| File | `//build/config/android/internal_rules.gni` |
| Symbol | `_kt_allowlist`, inside `java_library_impl` |

## The change

One path added to the allowlist:

```gn
_kt_allowlist = [
  "*/AsyncTabParamsManagerImpl.kt",
  "//third_party/androidx/*",
  "*/webengine_shell_apk/*",
  "//chrome/browser/ui/android/bricks/internal/*",

  # TaffyGo: the Compose status island and the Kotlin app layer it sits
  # on (decisions 0003 and 0014). All of it lives under the overlay at
  # //taffy, so this one line is the whole downstream cost of
  # Kotlin support and it moves only if that path moves.
  "//taffy/*",
]
```

The entry names the overlay root rather than any file inside it, so the
downstream delta stays one line no matter how many Kotlin files the island
grows.

## Why the overlay cannot host it

`java_library_impl` computes `_found_kt = filter_exclude(_abs_kt_files,
_kt_allowlist)` and asserts the result is empty. Both the list and the assert
live inside an upstream `.gni`, evaluated in a scope the overlay does not
participate in — a downstream `BUILD.gn` cannot append to a variable in a
template's private scope. There is no exemption argument, no `declare_args`
hook, and no per-target override.

The assert's own message says "Feel free to remove this assert when
experimenting locally," which is an invitation to a local hack rather than a
supported downstream seam. Deleting the assert instead of extending the list
would be a larger edit that also removes the check for everyone else.

## Why no specification preceded it

The nine specifications were written without a Chromium checkout, from the
upstream files' documented behaviour. This constraint is not documented
anywhere in Chromium — it is an assert inside a template — so no amount of
reading the public contract would have surfaced it. It was found the first time
`gn gen` loaded the overlay, which is exactly what
fork strategy §6 item 9
records: the 88-minute SP-01 build proved the toolchain and said nothing about
the overlay, because it never loaded it.

Two consequences worth keeping. The projected fork-debt figure is an estimate
built from what could be read, so it undercounts by construction — this patch
is the first evidence of by how much. And a number was needed that no
specification had reserved, which is why `export-patches` now reads a
`Taffy-Patch:` trailer from the commit instead of numbering by branch position.

## Rebase risk

**Low.** The list is data, several teams append to it, and a conflict is a
one-line reapply. The risk that matters is not conflict but deletion: if
upstream retires the allowlist and the assert — which the "for now" in its own
message invites — this patch retires with them and the queue shrinks by one.

**Retirement:** when upstream drops the Kotlin allowlist, or when TaffyGo stops
shipping Kotlin. Reviewed at each milestone rebase.

## Confirmed at SP-01

1. The allowlist exists at the pin with four entries, none matching
   `//taffy/*`.
2. All 71 `.kt` files in the overlay fail the assert without this patch; the
   first failure named the seventeen files in `:common_java`.
3. The same template asserts a `kotlin_stdlib` dependency on every target
   carrying a `.kt` source. That is **not** part of this patch — it is an
   product-root fix, applied through the shared dependency lists in
   `taffy-core/app/android/taffy_android_deps.gni`.

## Regenerating the patch

```bash
./tools/chromium/sync                    # leaves you on taffy/patched
# edit build/config/android/internal_rules.gni; commit with a Taffy-Patch: 0010 trailer
./tools/chromium/export-patches          # writes 0010-allow-Kotlin-sources-under-taffy.patch
./tools/check fast                       # reports patch count and modified lines
```
