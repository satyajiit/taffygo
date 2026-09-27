# 0001 — Register the TaffyGo product and test targets in the build graph

**Status:** `[Current]` — applied on 2026-08-17 as
`0001-register-the-TaffyGo-product-target-in-gn_all.patch`, one target only;
extended on 2026-08-23 with the host test binary; see "The change" for the
SP-01 narrowing
**Needed by:** WP-M1-01 — the product target must be buildable by name
**Estimated size:** ~8 modified upstream lines, 1 file

## Upstream file and symbol

| | |
|---|---|
| File | `//BUILD.gn` (the root build file of `chromium/src`) |
| Symbol | `group("gn_all")` — the product inside its `if (is_android)` branch, the test binary in its unconditional `deps` |

## The change

Add the TaffyGo product target to the dependency list of the root `gn_all`
group, beside the upstream Android product targets already listed there:

```gn
deps += [ "//taffy:product" ]
```

Nothing else changes. `//taffy:product` is the source-owning product aggregate
in `taffy-core/BUILD.gn`; on Android it reaches the real installable target at
`//taffy/app/android:taffy_public_apk`, plus the native browser, utility,
renderer, contracts and resources graph. It is not a forwarding label.

The edit sits inside the `!is_cast_android` sub-branch beside
`chrome_public_apk`, because that is where upstream lists Android browser
products at this milestone. Registering the aggregate rather than only the APK
also forces GN to load the root product graph, so a disconnected first-party
target cannot masquerade as part of the browser.

A second label goes in the group's unconditional `deps`, beside
`//sql:sql_unittests` and the other host suites:

```gn
deps += [ "//taffy:taffy_unittests" ]
```

It cannot sit beside the product. `//taffy:product` is registered only under
`is_android`, so on any other profile nothing names a label in
`taffy-core/BUILD.gn`, GN never loads that file, and every `//taffy:*` target
— `taffy_unittests`, `taffy_tests`, `taffy_browsertests`, `native`,
`contracts` — simply does not exist. The subdirectory build files are still
loaded, because patches 0006 and 0032 name `//taffy/browser` from
`//chrome/browser`, which is not Android-only; that is what made the absence
hard to see. `gn ls out/<host-profile> "//taffy/*"` lists 158 targets while
`gn ls out/<host-profile> "//taffy:*"` lists none.

The consequence was concrete: the committed `diag-sanitizer-x64` profile
(`is_asan` + `is_lsan`, `target_os = "linux"`) exists to measure leaks in
`taffy_unittests`, and `autoninja -C out/diag-sanitizer-x64 taffy_unittests`
answered `unknown target`. The leak instrument could not run on the suite it
was added for.

`taffy_unittests` is a host binary, so it belongs with the host suites rather
than with the Android product. Registering it also loads the root product
graph on every profile, which is what a future desktop UI layer needs before
it can have a GN graph at all.

## Why the overlay cannot host it

GN builds its graph from the root build file outward and loads a `BUILD.gn`
only when a label in it is reached. `//BUILD.gn` is the only registration
point, it is a file Chromium owns, and GN offers no downstream hook —
no include directory, no glob, no "extra targets" argument — that a mounted
overlay could use to add itself to a group in that file.

The alternative is to require every caller to know the label
(`autoninja -C out/dev-x64 taffy/app/android:taffy_public_apk`), which works for
a person typing a command but not for `gn analyze`, which CI uses to decide
which targets a change affects. A product target that `analyze` cannot see is
a product target CI does not build.

## Rebase risk

**Low.** `gn_all` is a list of labels that upstream appends to constantly, so
the surrounding context changes often but the change itself is three lines
inside an `if (is_android)` branch that has existed for years. Expect an
occasional context conflict at a milestone rebase, resolved by moving the
three lines; expect no semantic conflict.

**Retirement:** permanent. Reviewed at each milestone rebase.

## Verify at SP-01

1. That `group("gn_all")` is still the registration point at the pin, and that
   its Android branch is still spelled `if (is_android)`. Read `//BUILD.gn`
   and find where `//chrome/android:chrome_public_apk` is listed — that is the
   line the three labels go beside.
2. Whether `gn analyze` needs anything beyond `gn_all` membership for a
   Chromium builder machine.
3. Whether `//taffy/app/android/javatests:taffy_public_test_apk` needs
   to be listed in a test-specific group as well, so that the test lane picks
   it up. It is not one of the three labels this patch registers: it landed on
   2026-08-19 beside the sources it packages, reached through
   `//taffy/app/android:taffy_device_tests`.
4. That the unconditional `deps` list is still where upstream keeps its host
   unit-test binaries, and that `//sql:sql_unittests` is still in it — that is
   the line `//taffy:taffy_unittests` goes beside.

## Regenerating the patch

```bash
./tools/chromium/sync                    # leaves you on taffy/patched
# edit src/BUILD.gn in the checkout; commit with an owner and a reason
./tools/chromium/export-patches          # writes 0001-register-product-targets.patch
./tools/check fast                       # reports patch count and modified lines
```
