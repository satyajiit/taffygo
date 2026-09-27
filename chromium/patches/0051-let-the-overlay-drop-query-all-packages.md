# 0051 — Let a downstream manifest drop the package-query permission

**Status:** `[Current]` — exported; the merged manifest of a built artifact is
the evidence, and the device half is owed
**Needed by:** decision
0153
**Estimated size:** 2 added upstream lines, 1 file

## Upstream file and symbol

`//chrome/android/java/AndroidManifest.xml`, the
`android.permission.QUERY_ALL_PACKAGES` declaration and the comment above it.
The file is a jinja template every Android product manifest in the tree
extends, including TaffyGo's
`taffy-core/app/android/AndroidManifest.xml.jinja2`.

## The change

Wrap the comment and the permission in a block:

```text
{% block query_all_packages_permission %}
<!-- Needed to determine whether an app installed on the device should handle a web navigation.  -->
<uses-permission android:name="android.permission.QUERY_ALL_PACKAGES" />
{% endblock %}
```

Two added lines. The permission and its comment are unchanged, and every
template that does not override the block — Chrome's own, and every other
product in the checkout — renders exactly what it rendered before. TaffyGo's
overlay overrides it with nothing and declares the `<queries>` decision 0153
names in its place.

## Why the overlay cannot host it

The overlay is already the right place to *decide* this, and it cannot be the
place to *carry* it, for a reason that took a built artifact to establish.

`{% extends %}` can only override a block upstream declares, and upstream
declares none around this permission: the blocks nearest it are
`extra_uses_permissions`, which appends after every upstream permission, and
`extra_keyset_definitions`. So there is nothing to override.

The Android merger's `tools:node="remove"` does not reach it either. A node
operation applies to elements arriving from *another* manifest — a library's,
or a lower-priority one. After jinja renders the two templates there is one
document, the permission is declared in it, and the marker is ignored without
a message. That was tried first and the evidence is the merged manifest of the
release bundle: `out/release-arm64/gen/taffy/app/android/
taffy_public_base_bundle_module/AndroidManifest.merged.xml` carried the
permission while the overlay carried the removal. `build/android/gyp/
merge_manifest.py` has no permission-removal argument of its own.

That leaves the upstream file, and a block is the smallest durable edit to it.

## Rebase risk and retirement

**Low.** Two added lines around a declaration that has been stable for years,
in a file three other TaffyGo patches already touch (0022, 0025 and this one),
so a conflict here is seen by whoever rebases those. If upstream moves or
renames the permission, the patch fails loudly at `sync` rather than silently:
a block that is never rendered is a jinja error, not a quiet no-op.

Retire it when upstream declares its own hook around the permission, or when
Chromium's Android manifest stops requiring it. Reverting it does not restore
the old product behaviour by itself — the overlay's `<queries>` would stay, and
the permission would come back with them — so a retirement removes both halves
together and amends decision 0153.

## Export and verification

The one-file edit was committed on `taffy/patched` with `Taffy-Patch: 0051`
and exported by `./tools/chromium/export-patches`.

Verification is the merged manifest, because that is the only place the
removal exists:

```bash
autoninja -C out/release-arm64 \
  taffy/app/android:taffy_public_base_bundle_module__merge_manifests
grep -c QUERY_ALL_PACKAGES \
  out/release-arm64/gen/taffy/app/android/taffy_public_base_bundle_module/AndroidManifest.merged.xml
```

Zero, with the overlay's four `<queries>` entries present. That was first
observed with Play Billing vendored, when a fifth entry — the library's own
`InAppBillingService.BIND` — stood beside them and `./tools/play preflight` on
the signed bundle named five warnings rather than six.

The library has since been removed from the product, and the merged manifest
of a `dev-arm64` `taffy_public_apk` built on 2026-09-19 after that removal
carries no `QUERY_ALL_PACKAGES`, no `InAppBillingService`, no `billingclient`
token at all, and the overlay's four entries with no fifth. It also carries
three the overlay does not write — `com.google.android.aicore`,
`com.google.android.gms.policy_cast_dynamite` and a `BROWSABLE` `https`
view — which upstream contributes on merge, so "four" counts what this
template declares and not what the artifact ends up with. The preflight
count has not been re-measured since that first run and the number above is
from it, not a prediction about this one.

The source half — that the overlay still overrides the block, and still
declares the queries — is asserted on any host by
`taffy-core/app/android/tools/check_product_manifest.py`, in
`./tools/check fast --only mount`.
