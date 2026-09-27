# `:core:task`

**Status:** `[Current]` Android's immutable task projection and profile-scoped
command port over the generated browser Core API.

This module contains no reducer and mints no authority. `CoreTaskRepository`
projects browser-owned state and sends typed intent to `CoreApiClient`; the
sandboxed Rust task engine remains canonical. `TaskBindings` owns the one real
profile binding used by the product graph.

`TaffyReadinessRepository` answers one question the AI surfaces share: can
Taffy reach a provider at all. `taffyReadiness` is the pure rule, over
`ProviderReadinessFacts` — a usable key or an own address in the core's
roster, a plan behind a signed-in account, and the handles the browser holds
that the core has not confirmed — and the route the person chose on screen
SCR-004, which names a variant of the answer but never grants it. Readiness is
therefore a fact and never the saved preference. The real binding lives in the
application graph (`ProfilePlatformBindings.provideTaffyReadiness`), because
two of its inputs are ports of `:core:credentials` and `:core:preferences`,
which this module does not import; the factory takes them as flows.
