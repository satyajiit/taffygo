# `:core:assets`

**Status:** `[Current]` Android's projection of what TaffyGo downloads for
itself, and the typed intents a person can express about it.

These are not the person's downloads. They are the parts the product fetches so
it can work — a Python library, a model, a block list — and the module exists so
that nothing about a device's storage or a data allowance is spent out of sight.

Android decides nothing here. `TaffyPartsRepository` projects immutable
Rust-owned delivery state, carries the version the current snapshot names on
every intent, and echoes the browser's own reading of what the connection costs
rather than forming one. Live byte counts arrive on a separate flow because they
come by a separate route: the browser pushes them as the bytes land and the
isolated core is not asked, since nothing is decided from a figure that moves
several times a second. `TaffyPartsBindings` owns the profile scope.

Adding a part to the product is a row in the delivery catalog. Nothing in this
module is written per part — the only per-part code anywhere in the app is one
case in the projection and one name in the string catalogue.
