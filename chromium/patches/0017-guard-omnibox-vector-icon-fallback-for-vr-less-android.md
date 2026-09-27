# 0017 — Guard the omnibox vector-icon fallback for VR-less Android

**Status:** `[Current]` — applied 2026-08-18, found by the first build with
the decision 0019 args fragment
**Needed by:** decision 0019 — removing the XR runtimes (ARCore is a Google
Play Services dependency) collapses the derived `enable_vr` to false, and an
Android build without `ENABLE_VR` does not compile upstream as shipped
**Estimated size:** ~21 modified upstream lines, 2 files

## Upstream files and symbols

| | |
|---|---|
| File | `//chrome/browser/ui/omnibox/omnibox_edit_model.cc` |
| Symbol | `OmniboxEditModel::GetMatchIcon`, its vector-icon fallback |
| File | `//chrome/browser/ui/webui/cr_components/searchbox/searchbox_handler.cc` |
| Symbol | `CreateAutocompleteMatches` (match icon path) and its pedal-action icon loop |

## The change

`AutocompleteMatch::GetVectorIcon` is declared only under
`(!BUILDFLAG(IS_ANDROID) || BUILDFLAG(ENABLE_VR)) && !BUILDFLAG(IS_IOS)`
(`components/omnibox/browser/autocomplete_match.h`), but the fallback at the
tail of `GetMatchIcon` calls it unguarded. Wrap the fallback in the same
condition the declaration carries, returning the empty image on the excluded
configuration — the same value the favicon path above produces while a fetch
is outstanding:

```cpp
#if !BUILDFLAG(IS_ANDROID) || BUILDFLAG(ENABLE_VR)
  bool is_starred_match = IsStarredMatch(match);
  const auto& vector_icon_type = match.GetVectorIcon(is_starred_match, turl);
  return controller_->client()->GetSizedIcon(vector_icon_type,
                                             vector_icon_color);
#else
  return gfx::Image();
#endif
```

plus the explicit include of `components/omnibox/browser/buildflags.h`,
which owns the omnibox `ENABLE_VR` flag the header's own guard reads.

The searchbox WebUI handler has the same defect twice: the match icon path
calls `AutocompleteMatch::GetVectorIcon` unguarded (the mojom field keeps
its default empty value under the guard), and the pedal-action loop calls
`OmniboxAction::GetVectorIcon`, which exists only under
`SUPPORT_PEDALS_VECTOR_ICONS` — the same condition by another name
(`components/omnibox/browser/actions/omnibox_action.h` defines it from the
identical `(!IS_ANDROID || ENABLE_VR) && !IS_IOS` expression). The action
loop keeps its bitmap branch and skips only the vector fallback.
`AutocompleteIconToResourceName` itself compiles unguarded because the
omnibox vector-icon *definitions* are compiled on Android either way; only
the two accessors are gated.

## Why the overlay cannot host it

The unguarded call is inside an upstream function body; no downstream code
can add a preprocessor guard to a file it does not own. The alternative —
keeping `enable_vr = true` so the symbol exists — was tried first and fails
differently: the native WebXR side then compiles and registers JNI for Java
classes (`XrActivityListener`, `XrSessionCoordinator`) that the runtime-off
dex does not carry, and the JNI assertion stops the APK build. Consistency
requires the whole VR graph off, and that requires this guard.

## Rebase risk

**Low.** The guard mirrors the declaration's own condition, so the two move
together; if upstream refactors the icon path the patch conflicts loudly and
re-lands in whatever replaced it.

**Retirement: upstream it.** `enable_vr = false` on Android is a
configuration upstream's own `declare_args` invites ("Embedders can still
override any of the particular runtimes") and its Android compile is broken
as shipped — a small CL of a kind upstream accepts. Attempt after the shape
survives one milestone rebase.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit omnibox_edit_model.cc in the checkout; commit with the Taffy-Patch
# trailer
./tools/chromium/export-patches
./tools/check fast
```
