# Shared design tokens

`tokens.json` is the single machine owner for TaffyGo's semantic colour
values. It is platform-neutral and records 32-bit ARGB words so opacity is exact
on every projection.

`generate.py` is standard-library-only and emits three committed projections:

- Kotlin `PaletteValues` for the current Compose UI;
- CSS custom properties for a future WebUI surface;
- a dependency-free C++ header for future Chromium Views code.

The projections are generated portability assets, not desktop product targets.
They must not be edited by hand. Regenerate and verify them with:

```bash
python3 taffy-core/resources/tokens/generate.py --write
python3 taffy-core/resources/tokens/generate.py --check
python3 taffy-core/resources/tokens/generate.py --self-test
```

Only wash tokens (the semantic washes and the three ribbon washes of decision
0103), scrim and selection may be translucent. The generator checks
that both themes have the same closed vocabulary and that every other token is
opaque.
