# `//taffy/resources/branding`

**Status:** `[Decided]` mechanism (decisions
0001 and
0012);
the palette and iconography are decided by
0025
**Implementation status:** `[Current]` the two generators run, and every target
in this directory is reachable from the migrated product graph. A complete
post-cutover `taffy_public_apk` build and install has not yet been recorded, so
the old installed package does not prove these targets are packaged. The
layers listed under "What is missing" below are still missing; reachability
does not conjure an asset nobody drew
**Owning milestone:** M1, work package WP-M1-01 in the
implementation plan
**Authority:** brand for naming and
tone; [chromium/README.md](../../../chromium/README.md) for the licensing and branding
obligations a Chromium derivative inherits

A Chromium derivative inherits the BSD licence and every third-party licence
obligation, and inherits none of the Google-owned branding: not the Chrome
name, not the logo, not the Google-only services. TaffyGo therefore ships its
own name, its own icon, and its own attribution. This directory is that
replacement, in one place, so a reviewer can read the whole of it without
reading the product target.

It also carries the one thing a person is entitled to know about a browser
built by someone other than Chromium: which upstream revision it is, and how
far it has been changed.

## What is here

| Path | What it is |
|---|---|
| [`taffy_branding.gni`](taffy_branding.gni) | The application id, the artifact names, and the resource directory that supplies them. Every value appears exactly once in the build graph. |
| [`java/res/values/`](java/res/values/taffy_branding_strings.xml) | `app_name` and the widget titles. Proper nouns, never translated. |
| [`java/res-debug/values/`](java/res-debug/values/taffy_branding_strings.xml) | The same resource for a debug build, so two installs are distinguishable on one launcher. |
| [`java/res_images/`](java/res_images/drawable/themed_app_icon.xml) | The committed XML shadows: the adaptive-icon layers, the notification glyph, the install-promo art, and an empty animation replacing the sign-in spinner. Structure, not artwork — every raster byte they reference comes from the generator. |
| [`icons/manifest.json`](icons/manifest.json) | The asset set: which geometries exist, derived from which committed mark, and whether each carries TaffyGo's own name (`launcher`) or shadows an upstream one (`override`). |
| [`tools/generate_icons.py`](tools/generate_icons.py) | Derives every raster asset from the masters. `--generate`, `--check`, `--list`. |
| [`tools/check_shadowed_names.py`](tools/check_shadowed_names.py) | Asserts every shadowed upstream resource and string name still exists at the pin; run by `./tools/chromium/build` before every product build. |
| [`tools/png_io.py`](tools/png_io.py) | An 8-bit RGBA PNG reader, writer and box resampler, standard library only. |
| [`tools/write_upstream_provenance.py`](tools/write_upstream_provenance.py) | The PAR-SEC-002 report: a C++ translation unit for the product and a JSON sidecar for the release manifest. |
| [`tools/repo_root.py`](tools/repo_root.py) | Finds the repository from inside the overlay mount, and writes the depfile that keeps the two generators correctly rebuilt. |
| [`taffy_upstream_provenance.h`](taffy_upstream_provenance.h) | The committed API the generated translation unit fills in. |

## Two rules a reviewer should check first

1. **No committed artwork.** Every launcher icon is derived from
   `brand/png/taffygo-mark-color-on-dark.png` by
   [`tools/generate_icons.py`](tools/generate_icons.py) at build time. A PNG
   appearing in this directory would be an unreviewable blob and a second
   source of truth for the mark. `--check` compares a produced set against the
   master byte for byte, which is possible because the encoder is
   deterministic.
2. **No hand-written pin.** The provenance values come from
   `chromium/REVISION`, `chromium/SECURITY_PATCH_LEVEL` and
   `chromium/patches/`, counted the same way `./tools/check fast` counts them,
   so the number the product reports and the number that blocks the build
   cannot disagree.

## How the substitution actually works

Renaming a Chromium derivative is usually assumed to be a patch. It mostly is
not:

- **The name** is an Android resource. `branding_resources` is declared with
  resource-overlay precedence, so its `app_name` replaces the upstream value
  of the same name. No upstream file changes.
- **The application id** is a build value,
  `com.taffygo.browser` (`.debug` appended for debug builds), passed to the
  product target from `taffy_branding.gni`.
- **The icons and product images** are substituted by name.
  `image_override_resources` shadows the upstream launcher-icon family
  (`app_icon`, the `layered_app_icon` adaptive layers, `themed_app_icon`),
  the FRE logos (`fre_product_logo`, `product_logo_name`), the notification
  glyph (`ic_chrome`), the menu and install-promo art, and the sign-in
  spinner animation — so no Chromium artwork is packaged and every consumer,
  including the Java call sites that read `R.mipmap.app_icon` directly, gets
  the TaffyGo mark. This **reverses an earlier stance**: the icon was once
  deliberately named `taffygo_launcher` to avoid shadowing, but at the
  pinned milestone the manifest points at adaptive-icon indirections and
  three Java call sites read the upstream name, so a non-shadowing icon
  fixes two consumers of seven and still ships Chromium bitmaps. The
  reversal and its evidence are recorded in
  [`chromium/patches/0002-downstream-branding-hooks.md`](../../../chromium/patches/0002-downstream-branding-hooks.md).
  The shadowing risk — an upstream rename silently un-brands the product —
  is answered by [`tools/check_shadowed_names.py`](tools/check_shadowed_names.py),
  which `./tools/chromium/build` runs against the checkout so a rename fails
  the build loudly. `taffygo_launcher` remains as TaffyGo's own resource
  name for surfaces the product controls.

## What this directory deliberately does not do

- **It does not decide the palette.** Colour tokens, the Material You stance
  and the in-app icon set belong to
  record 0025
  and the screen catalog.
  Nothing here contains a hex value, and that stays true now that the values
  exist: the launcher background named below is read from the record, not
  copied into this directory's prose.
- **It does not own a version.** Chromium's own version machinery owns the
  product version; this directory reports the *upstream* revision, which is a
  different fact.
- **It does not own the product name in C++.** That is
  `//taffy/common/public/taffy_product_identity.h`, because a GN value
  cannot be unit tested and that seam is.
- **It does not build the product.** `//taffy/BUILD.gn` declares
  `taffy_public_apk`, `taffy_public_test_apk` and `taffy_public_bundle`; this
  directory supplies their identity.

## What the Chromium track must verify, in order

Each item is also marked with a `VERIFY AT SP-01` comment at the line it
affects. Grep for `VERIFY AT` to get the live list; this is the ordered
summary, cheapest and most blocking first.

1. **The product template.** That
   `chrome/android/chrome_public_apk_tmpl.gni` exists at the pin and exports
   `chrome_public_apk_or_module_tmpl`, and that every argument
   `//taffy/BUILD.gn` passes it is spelled the way that file spells
   it. Nothing else in this directory matters if the product target does not
   generate.
2. **Resource overlay.** That `android_resources` still takes
   `resource_overlay` in `build/config/android/rules.gni`, and that it
   accepts generated files under a `res/` directory in `$target_gen_dir`. If
   it does not, patch 0002 grows from the icon reference to the whole
   substitution and its estimated size doubles.
3. **`read_file` with the `json` input conversion**, used to make
   `icons/manifest.json` the single source of the density set. If the
   conversion is unavailable at the pinned GN, `generate_icons.py` emits a
   committed `.gni` instead and a presubmit keeps the two in step.
4. **Out-of-tree depfiles.** That Siso as well as Ninja honours a depfile
   naming absolute paths outside the source root. This is what keeps the icons
   and the provenance correctly rebuilt when the artwork or the pin changes.
   If it is rejected, `./tools/chromium/sync` writes the values into the mount
   at sync time instead, and that trade belongs in the SP-01 report rather
   than being decided here.
5. **The resource id range.** `taffy_strings.grd` needs a `first_id` that
   cannot collide with an upstream catalogue, and upstream allocates ranges
   centrally in `tools/gritsettings/resource_ids.spec`. That allocation is
   part of patch 0003.
6. **The upstream launcher-icon resource name.** If it is stable and
   overridable, renaming the generated icons to match removes the icon half of
   patch 0002 entirely — a one-line change in `icons/manifest.json`. Measure
   before choosing: a shadowed upstream resource is cheap now and expensive at
   every rebase.

## Blocked on a decision, not on effort

These are named here rather than guessed, because each needs a value this
repository has not decided and inventing one would put an unreviewed brand
choice into a shipping artifact.

1. **Adaptive icon** (foreground, background and themed-monochrome layers).
   Android composites an adaptive icon over an opaque background layer, which
   is a flat brand colour. That colour now exists:
   record 0025
   §7 names the launcher icon as the colour-on-dark mark on the dark theme's
   `surface`, and the record is the one place that value is written down. What
   is still missing is the drawing work, not the decision — the committed marks
   are transparent and the concept sheet's tile is transparent too, so the
   layers have to be generated against that background. Until they are, the
   product ships the legacy square launcher icon, which is complete and valid.
2. **Round icon** (`android:roundIcon`). Same reason: a round icon is
   full-bleed, so it needs the same opaque background.
3. **Store listing icon.** Play renders it without a launcher mask, so it also
   needs the opaque background. It is a release-artifact input rather than a
   build input, and it belongs with the release mechanics of WP-M1-07.
4. **Notification small icon.** Derivable today —
   `png_io.Image.to_monochrome_alpha` exists for exactly this — but nothing
   renders a notification before M4 (PAR-AND-006), and a resource with no
   consumer is dead weight that a rebase still has to carry.
5. **The instrumentation suite in `taffy_public_test_apk`.** Landed on
   2026-08-19, and not where this item expected it. The target is declared at
   `//taffy/app/android/javatests:taffy_public_test_apk`, beside the
   sources it packages and the instrumentation manifest it generates, rather
   than as an edge added to `//taffy/BUILD.gn`. The comment on the
   product targets in that file records why.

## Running the generators

```bash
# The launcher icon set, into a build directory.
python3 taffy-core/resources/branding/tools/generate_icons.py \
    --generate --out-dir out/branding

# Verify a produced set still matches the committed mark.
python3 taffy-core/resources/branding/tools/generate_icons.py \
    --check --out-dir out/branding

# The upstream provenance report, as a person reads it.
python3 taffy-core/resources/branding/tools/write_upstream_provenance.py \
    --print
```

Both resolve the repository root through the overlay mount, so they run
identically from a checkout and from this repository. Pass `--repo-root` to
override.
