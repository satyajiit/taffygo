# Product resources

This directory owns platform-neutral product resources and their projections:

- `branding/` derives launcher identity and upstream provenance;
- `catalog/` owns externalized browser and Android strings;
- `tokens/` owns semantic visual values and generates Kotlin, CSS and C++.

Platform code consumes generated or build-projected resources. It does not
copy a string, token value or icon master into an operating-system adapter as
a new source of truth. Adding a future macOS, Windows, Views or
WebUI surface therefore adds a projection, not a second design vocabulary.

The `resources` component is process-neutral and cannot depend on browser,
renderer, utility-service or UI implementation code.
