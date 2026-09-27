# TaffyGo toolchain manifest

**Status:** `[Current]` — a row carries a version when its machine owner file
records one, and `TO-VERIFY` when the component that owns it does not exist
yet. Structure decided by decision
0013.

**Rule:** this file is the **only human-readable index** of versions. Every
version has exactly one **machine owner file**, and the owner file is the
truth — this table is a rendering of it. A version bump edits the owner file
*and* this row together, by hand, because nothing automated opens one
(decision 0023). `./tools/check
fast --only pins` verifies both directions: the row must carry the owner
file's value, and it must name that owner file. A row that records a version
no check reads is reported, so a pin cannot enter this table without a check
behind it. No other document quotes version numbers.

## Update policy

- **Track latest stable for every row.** One component per change, by hand:
  nothing opens an update automatically (decision
  0023). A bump edits the machine
  owner file and the row here together, and the `pins` lane verifies both
  directions.
- The **Chromium row moves only via the milestone-rebase procedure** in the
  fork and build strategy
  (~every 4 weeks, within 5 working days of upstream stable).
- Inside the Chromium build, **Chromium's bundled toolchain (SDK, NDK, JDK,
  Clang, Rust, GN, Siso) overrides every row below** — those rows govern the
  Android host loop, host tooling, services, and website only.
- Chromium pin recorded 2026-08-16: 152.0.7977.42, the current Android
  stable (released 2026-08-12) — inside the 5-working-day rebase window.
- Latest-stable snapshot verified 2026-08-16 (re-verify at pin time):
  Next.js 16.3 · Tailwind CSS 4.3.3 · lucide-react 1.31 · React 19 ·
  AGP 9.3 · Kotlin 2.4 · Rust 1.97.1 (1.98 due 2026-08-20) · Node 24 LTS ·
  pnpm 11.21.
- Where a row below trails that snapshot, the row is what the component is
  actually built and tested with, and the weekly update PR is the mechanism
  that closes the gap. This table never records an aspiration.

## Pins

| Component | Pin | Machine owner file |
|---|---|---|
| Chromium revision (stable milestone tag) | 152.0.7977.42 @ db8ceb709fe92f3bb010fb982d6300e54de6dc6a | `chromium/REVISION` |
| Chromium security patch level | 0 (no cherry-picks carried) | `chromium/SECURITY_PATCH_LEVEL` |
| depot_tools revision | 13febbee9ece9e03df923f69d540afc63c6db93e | `tools/pins/depot_tools` |
| Android Gradle Plugin | 9.3.1 | `gradle/libs.versions.toml` |
| Gradle | 9.7.0 (checksum-verified by the wrapper) | `gradle/wrapper/gradle-wrapper.properties` |
| Kotlin | 2.4.10 (Compose and JVM plugins) | `gradle/libs.versions.toml` |
| Compose BOM | 2026.08.00 (requires the compileSdk row below) | `gradle/libs.versions.toml` |
| JDK (Android UI) | 21 (Gradle toolchain) | `gradle/libs.versions.toml` |
| Android compileSdk | 37 | `gradle/libs.versions.toml` |
| Android targetSdk | 37 | `gradle/libs.versions.toml` |
| Android minSdk | 29 floor (upstream `chrome_public_apk` floor; final floor: OD-017) | `gradle/libs.versions.toml` |
| Dagger | 2.60.1 | `gradle/libs.versions.toml` |
| CommonMark Java (core and GFM tables) | 0.30.0 | `gradle/libs.versions.toml` |
| KSP | 2.3.11 | `gradle/libs.versions.toml` |
| androidx SplashScreen | 1.2.0 | `gradle/libs.versions.toml` |
| Space Grotesk (variable font asset, OFL) | google/fonts main @ sha256 acad6de1fc93436f5c0f1f4137751ef04f1aea3063e7036535970ffcfbd79f72 | `taffy-core/ui/android/core/designsystem/vendor/space-grotesk.txt` (the Android UI asset; `website/` carries its own copy) |
| Phosphor Icons (ImageVector subset, MIT) | core @main, a subset of 80 glyphs | `taffy-core/ui/android/core/ui/vendor/phosphor.txt` |
| Provider brand marks (ImageVector subset, trademarks) | Simple Icons 16.28.0 and lobe-icons v5.16.0, fifteen marks — eight from the first and seven from the second, the split recorded per mark because the file licence differs and the trademark position does not — sha256 9c321417a14ec47528c24205212d4752ad4fcf8d8ddd8626f96499c941bd6256 of the committed file | `taffy-core/ui/android/core/ui/vendor/provider-marks.txt` |
| cwebp (Android launcher icons) | 1.3.2 | `taffy-core/ui/android/tools/icons/manifest.json` |
| flag-icons (country-flag pack, MIT) | 7.5.0, the 4x3 set, 249 ISO 3166-1 entries; pack sha256 46d06699bd17d6943eeb2d4540b707eae34c6313ac01d1835c61201b2decb5b5 (decision 0047) | `taffy-core/third_party/flag-icons/tools/manifest.json` |
| librsvg (country-flag pack rasteriser) | 2.58.0, refused on mismatch because the digest moves with it | `taffy-core/third_party/flag-icons/tools/manifest.json` |
| cwebp (country-flag pack) | 1.3.2 | `taffy-core/third_party/flag-icons/tools/manifest.json` |
| Rust toolchain (portable host loop) | 1.96.0 | `rust-toolchain.toml` |
| ccache | 4.9.1 (90% ineffective at milestone 152 — see owner file) | `tools/pins/build-cache` |
| sccache | 0.17.0 (pinned but not yet exercised as a Rust cache: the GN Rust targets build under Siso without it) | `tools/pins/build-cache` |
| Node.js | `>=24.0.0 <25` | `package.json` |
| pnpm | `pnpm@11.8.0` (Corepack) | `package.json` |
| TypeScript (website) | 6.0.3 | `website/package.json` |
| Next.js | 16.3.1 | `website/package.json` |
| React | 19.2.8 | `website/package.json` |
| Tailwind CSS | 4.3.3 | `website/package.json` |
| lucide-react | 1.31.0 | `website/package.json` |
| Lighthouse | 13.4.1 (audit only; not shipped) | `website/package.json` |
| gplay (Play Console CLI) | 0.10.0 (release asset checksum-verified per host) | `tools/pins/gplay` |
| CPython (vendored interpreter, decision 0046) | 3.14.7 | `taffy-core/third_party/cpython/tools/manifest.json` |

## Reading the Android rows

Three facts about the rows above are load-bearing, and each is proved by
building them rather than asserted here.

1. **There is no Kotlin Android plugin row, on purpose.** Kotlin is built into
   the Android Gradle Plugin from AGP 9.0, and applying
   `org.jetbrains.kotlin.android` to an Android module is a hard configuration
   error at the AGP pin above. The Kotlin row therefore governs the Compose
   compiler plugin and the JVM plugin used by pure-JVM modules — not Android
   compilation, which follows the AGP row.
2. **The Compose BOM pin requires the compileSdk pin.** The pinned BOM refuses
   to compile against an older platform, so the two rows move together: the
   Android UI needs the SDK platform package for the recorded compileSdk
   installed, which `./tools/bootstrap --profile android` verifies and offers
   to install.
3. **The JDK row is the Gradle toolchain,** read from the same version catalog
   by every convention plugin in `build-logic`, so a JDK bump is one edit
   rather than one per module.

## Rows that are still `TO-VERIFY`

None today. The table stays, because a row here names the work that retires
it, and none of them blocks a lane — `./tools/check fast` skips what it cannot
run. A pin whose owner file cannot yet record a real value goes in as one row
of this shape and leaves when the retiring event has happened.

| Row | Retired by |
|---|---|
| *(no row carries the label)* | The last one, Supabase CLI, was retired on 2026-08-30 and its component deleted on 2026-09-19 — see below |

One row was removed rather than reworded. "CI runner base image" named a pin
whose owner was a hosted runner image, and decision
0023 abolished that image without
replacing it — so the row had no retiring event and could never have gained
one. It is not a pin this repository is missing; it is a pin this repository
no longer has a place for. The Chromium builder machine it was entangled with
is still an open cost item under OD-026, but a
machine is not a version and does not belong in this table. The `pins` lane
counts zero `TO-VERIFY` rows. Android native code is
built only through Chromium's pinned toolchain; the retired Gradle JNI path no
longer owns an NDK or cargo-ndk pin.

A second row left this table by being retired rather than removed. "Python"
carried `TO-VERIFY` against `.python-version` while no interpreter existed, and
it now names a real one:
[`taffy-core/third_party/cpython/tools/manifest.json`](taffy-core/third_party/cpython/tools/manifest.json)
pins the CPython that has been cross-built for both Android ABIs, run on a
device and measured. The owner file changed with the value, because the two
things `.python-version` could have meant have separated — that file is a pyenv
convention for the interpreter a checkout's own tooling runs under, which here
is whatever host Python 3 the tool suite finds and is deliberately not pinned.
Pinning the version is not the go/no-go: whether Python ships at all is still
spike SP-07's, under decisions
0007 and
0046.

A third row left the same way and then left the table entirely, which is
worth recording because the two departures mean different things. "Supabase
CLI" carried `TO-VERIFY` against `tools/pins/supabase-cli` because the
migrations and pgTAP assertions under `supabase/` had been executed without
the CLI build that executed them being recorded. It was retired on 2026-08-30
by somebody starting the local container stack and running the lane —
`supabase db reset`, `supabase db lint` and `supabase test db`, 13 files and
493 assertions, all passing under CLI 2.114.0, recorded in the verification
report section 2.4 — so the owner
file recorded a real value and the row was honest.

It is gone now for an unrelated reason: decision
0200 deleted the account
plane, so `supabase/`, the `supabase` lane and `tools/pins/supabase-cli` no
longer exist and neither does the "Local Postgres (Supabase stack)" row beside
it. That is a pin whose component left, not a pin that was never filled in —
the opposite of the `TO-VERIFY` case above, and the reason this paragraph does
not simply disappear with the rows.

## Related

- [The Chromium fork](chromium/README.md) — the operating procedure for the pins above
- Chromium fork and build strategy
- Decision 0012 — repository layout
- Decision 0013 — build acceleration and CI
- Developer setup
- [The command suite](tools/README.md) — the lanes that read this table
