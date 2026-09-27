# `:core:providers`

**Status:** `[Current]` Android's projection of the provider roster the
isolated core publishes (decision
`docs/decisions/0080-the-served-catalog-is-a-browser-file-and-a-core-merge.md`).

Android decides nothing here. The merged catalog — compiled baseline, served
overlay, the person's own providers — lives in the sandboxed core, and every
row a screen draws is the core's answer: which providers exist, what each
offers, whether this build can act on it, and whether the served catalog
tried to move one's address and was refused. `ProviderRosterRepository`
projects that roster off `CoreStatus` into the app's own vocabulary so no
feature names a generated wire type, and `ProviderRosterBindings` owns the
profile scope.

Adding a provider to the product is a catalog row, not code here. The only
per-vendor code anywhere in the app is a mark and its measured colour, which
stay compiled because a logo needs provenance a served document cannot carry.
