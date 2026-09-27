# 0002 — Let the Chrome Android product template carry downstream branding

**Status:** `[Current]` — exported and applied 2026-08-18. Revised the same
day against the pinned milestone: half one shrank because upstream now
forwards most of the identity, and half two (the manifest icon variable) is
**dropped** in favour of resource shadowing; see "The icon decision" below and
decision 0019's branding section. What the `.patch` contains is the revised
half one and nothing else, in the two files named below
**Needed by:** WP-M1-01 — the product ships as TaffyGo, not as Chromium
**Estimated size:** ~9 modified upstream lines, 2 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/android/chrome_public_apk_tmpl.gni` |
| Symbol | `template("chrome_public_apk_or_module_tmpl")` |
| File | `//chrome/android/java/src/.../base/SplitCompatApplication.java` |
| Symbol | `COMMAND_LINE_FILE` |

## The change

At the pinned milestone the template already forwards `manifest_package` and
`apk_name` — the arguments this specification originally proposed to add —
so half one reduces to the one identity value still hard-coded in the
template body:

```gn
# Downstream products supply their own command-line flags file; the default
# is the value the template hard-coded before the argument existed.
_command_line_flags_file = "chrome-command-line"
if (defined(invoker.command_line_flags_file)) {
  _command_line_flags_file = invoker.command_line_flags_file
}
```

The flags file has two readers, and both halves are needed: the GN argument
names the file the build's install/launch wrappers write, while the Java
constant `COMMAND_LINE_FILE` in `SplitCompatApplication` names the file the
browser reads at startup. Changing only the first (this patch's original
shape) silently disconnects `bin/taffy_public_apk argv` from the product —
found on-device when a written flags file was never picked up.

## The icon decision (half two, dropped)

The original half two pointed the manifest's `android:icon` at a
substitutable variable so TaffyGo could supply `@mipmap/taffygo_launcher`
without shadowing an upstream resource name. Verified against the pinned
milestone, that approach no longer reaches the goal:

- the manifest now points at adaptive-icon indirections
  (`android:icon="@drawable/ic_launcher"`), not at a bitmap the variable
  could replace;
- the Chromium icon family is packaged unconditionally by
  `chrome_base_module_resources`, so repointing the manifest would still
  ship Chromium artwork in the APK;
- live Java references `R.mipmap.app_icon` directly (app menu, suggestions
  cursor, contextual-search quick action), which no manifest edit touches.

Upstream's own mechanism for this is name-shadowing with
`resource_overlay = true` — the tree carries exactly such a target commented
"Overrides icon / name defined in chrome_app_java_resources". TaffyGo
therefore **reverses the earlier no-shadowing stance for branding images**:
`//taffy/resources/branding:image_override_resources` supplies TaffyGo artwork under
the upstream resource names, replacing every consumer at zero patch cost. The
known risk of shadowing — an upstream rename silently un-brands the product —
is mitigated by a repository check that asserts each shadowed name still
exists upstream at the pin, so a rename fails loudly instead. The mechanics
and the shadowed-name list live in
`taffy-core/resources/branding/README.md`.

## The GCM manifest entries (recorded, deferred)

The upstream manifest declares the GCM permission and receiver services
(`com.google.android.c2dm.*`). They are inert in the derivative — Web Push
cannot register without an FCM project — and their fate is owned by
OD-078: removing them now would foreclose the push-relay option and add a
manifest patch ahead of the decision; the entries stay until OD-078 decides,
and the store-review implications are recorded in that register row.

## Why the overlay cannot host it

The remaining edit changes a value the template assigns before the caller is
consulted, inside an upstream-owned file. Everything else this specification
once carried has moved to overlay mechanisms that need no patch: the name via
`branding_resources` (resource-overlay precedence), the icons and product
images via `image_override_resources` (same mechanism, decision recorded above).

## Rebase risk

**Low** (was Medium when the change touched a template body in two places
and a manifest). One additive, defaulted argument in one template; a
three-way merge succeeds unless the template is rewritten, and the nightly
rebase rehearsal surfaces that before it is due.

**Retirement:** candidate for upstreaming — a downstream-selectable
command-line flags file is useful to every derivative. Attempt once the shape
has survived two milestone rebases.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit chrome_public_apk_tmpl.gni in the checkout; commit with the
# Taffy-Patch trailer
./tools/chromium/export-patches
./tools/check fast
```
