# 0003 — Pack the TaffyGo string resources into the product

**Status:** `[Current]` for the id allocation — applied 2026-08-17 as
`0003-allocate-resource-id-ranges-for-the-TaffyGo-string-c.patch`, covering
**both** catalogues (`taffy_strings.grd` and `taffy_android_strings.grd`; the
android catalogue assigns message ids through the same registry, which this
specification originally missed), with starts 13000 and 13100 — above every
upstream range, so a rebase that grows an upstream catalogue cannot collide.
No `first_id` attribute is read back into either `.grd`: grit takes the range
from the registry through its `-f` flag, and a second copy of the number in
the overlay would be the drift this registry exists to prevent. `[Current]`
for the repack half too, applied 2026-08-21: the pattern
(`taffy_strings_`) and its dep joined the shared `source_patterns` list in
`chrome/chrome_repack_locales.gni` — the template every product's locale
repack goes through, which is what `chrome/BUILD.gn` in the original estimate
turned out to be at the pin — and the overlay's grit target became upstream's
own `grit_strings` wrapper so it emits the per-locale (and per-gender) paks
the repack consumes, with every non-English pak carrying the English strings
until OD-060 sets the launch languages
**Needed by:** WP-M1-01 and PAR-L10N-001 — a string the product cannot load is
not externalized, it is lost
**Estimated size:** ~14 modified upstream lines, 2 files

## Upstream files and symbols

| | |
|---|---|
| File | `//tools/gritsettings/resource_ids.spec` |
| Symbol | the top-level id allocation map |
| File | `//chrome/BUILD.gn` |
| Symbol | the locale repack rules that assemble the product's `.pak` set |

## The change

Two entries, both additive.

### Allocate a resource id range

`//taffy/resources/catalog/taffy_strings.grd` needs a `first_id` that
cannot collide with any upstream catalogue, and upstream allocates every range
centrally so that two catalogues can never be given the same one:

```python
"taffy/resources/catalog/taffy_strings.grd": {
    "messages": [<next free start>],
},
```

The allocated value is then read back into the `first_id` attribute of
`taffy_strings.grd`, which is an overlay file.

### List the generated pak

Add `taffy_strings_<locale>.pak` to the repack rule that builds the product's
locale paks, beside the Chrome catalogues already listed there.

## Why the overlay cannot host it

Both files are registries. `resource_ids.spec` exists precisely so that no
catalogue chooses its own range — a downstream catalogue that picked one
unilaterally would be choosing the one thing the registry exists to prevent.
The repack lists are the same shape of problem as patch 0001: a pak is not
loaded because it was built, it is loaded because a rule that Chromium owns
names it, and GN offers no downstream hook to append to that rule.

## Rebase risk

**Low, with one sharp edge.** Both edits are single entries in lists, so
context conflicts are rare and mechanical. The sharp edge is the id range:
upstream may reassign ranges wholesale during a rebase, and a stale `first_id`
does not fail loudly — it produces overlapping resource ids. The mitigation is
that grit itself refuses overlapping ranges when it can see both catalogues,
which the product build always can.

**Retirement:** permanent while TaffyGo ships strings of its own. Reviewed at
each milestone rebase, with the id range re-read from the spec rather than
assumed.

## Verified at SP-01 (answers recorded 2026-08-21)

1. `tools/gritsettings/resource_ids.spec` is still the allocation mechanism
   at the pin; the allocated starts are in the status line above.
2. The repack target is the `chrome_repack_locales` template in
   `chrome/chrome_repack_locales.gni`, whose shared `source_patterns` list
   every product invocation inherits — one edit covers APK and bundle alike.
3. An entry per locale, declared in the `.grd`'s own outputs; upstream's
   `grit_strings` GN template expands the same set (with gender variants
   where the platform enables them) and grit asserts the two lists agree.
4. Not re-opened: the patch stayed two entries in two registries, so the
   Android-resources alternative would now cost more than it saves.

## Regenerating the patch

```bash
./tools/chromium/sync
# edit the two files in the checkout; commit as one logical change
./tools/chromium/export-patches
./tools/check fast
```
