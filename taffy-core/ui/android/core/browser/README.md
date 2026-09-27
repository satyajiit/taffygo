# `:core:browser`

**Status:** `[Current]` Narrow UI ports for Chromium-owned tabs, navigation,
downloads, and the live page surface.

`BrowserMediator` is supplied at Window construction and remains scoped to that
window. `BrowserBindings` creates the one Window-scoped `BrowserRepository`.
Compose can observe immutable projections and request closed browser operations;
it cannot reach a Profile, Tab, WebContents, URL loader, cookie jar, or
browser-native pointer.

`BrowserProfilesRepository` is the port over Chromium's regular profiles: list,
create, switch and delete, projected without filesystem paths. It lives here
rather than in the settings feature because two features read it — settings
for SCR-708 and workspaces for the profile line on SCR-304 (decision
0102).
The shell's `ChromiumBrowserProfilesRepository` implements it, and replaces a
name Chromium chose (`Profile.usesDefaultName`, "Your Chromium" for the first
profile) with the product's own before either screen reads it. 1.0 ships one
profile, so no screen calls create or switch (decision
0255).

The main source set contains only the Chromium-owned port. Deterministic
mediators and reserved fixture hosts live under `src/test` only. `NoPageSurface` is the safe
Compose-local absence value before a native page host is attached; it performs
no browser operation and owns no state.
