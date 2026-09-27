# `:app`

**Status:** `[Current]` The Compose product projection, Dagger component
definitions, navigation host, and Android-native adapters. Owning milestone:
M0, WP-M0-07.

The shipping owner is Chromium. `TaffyBrowserActivity` obtains the exact
regular profile runtime from the browser-owned `TaffyProfileRuntimeProvider`,
opens one Window component, and closes it with the Activity. Private tabs in
that window use a separate private profile graph through the profile-owned
Tab/WebContents observer. The observer keeps every Tab component independent
of its current window. This module
contains no `Application`, Activity, process-global active-profile slot, or App
Startup provider.

```text
TaffyProcessComponent
├── TaffyProfileComponent (regular)
│   ├── TaffyWindowComponent
│   └── TaffyTabComponent
└── TaffyPrivateProfileComponent
    └── TaffyTabComponent
```

The regular Profile builder accepts only browser-owned ports and opaque identities:
Core API, page intelligence, preferences, workspaces, provider secret handles,
credential secure-storage, and trusted auth-redirect ingress. Window and Tab
are siblings because a tab may move between windows. Each owner closes its
lifetime exactly once. The distinct private builder accepts only its opaque
identity; its graph installs no account, preference, credential, secure-store,
analytics, workspace, task, or Window modules.

Android-specific code stays here:

- Compose navigation and layout projection;
- runtime-permission requests and typed results;
- Credential Manager and immediate exchange of credential bytes for an opaque
  browser secure-store handle;
- Custom Tabs as the visible authorization surface.

One entry left that list on 2026-09-20 — callback filtering for
`com.taffygo.browser://auth`, after which only the trusted profile-bound
browser ingress saw the bounded raw URI. `AndroidAuthRedirectAdapter` was
deleted with the account intent ingress, and Chromium patch 0022 no longer
registers the filter, so no Android code in this module filters a callback and
no filter delivers one. A vendor's redirect is claimed by a navigation
throttle inside the page instead, which never reaches Android at all.

`AndroidBackupDocumentAdapter` owns the document-provider transfer for an
already encrypted `.aib` archive (decision
0122).
Its native staging interface supplies detached file descriptors, authenticates
the complete readback, and releases the operation. Android copies a fixed-size
buffer at a time, closes and reopens the selected document, checks exact length
and EOF, and reports success only after native verification. Neither a recovery
key nor archive plaintext reaches this adapter. Deletion is explicit and
requires a successful provider deletion followed by a missing document;
permission loss or an unavailable provider cannot stand in for absence.
`CreateBackupDocument` and `OpenBackupDocument` accept only granted content
URIs and retain no persistent grant. `AndroidBackupImportAdapter` copies an
unknown-length encrypted input into a native-owned stage under the operation's
authorized byte bound, without trusting provider metadata or parsing the
archive in Kotlin. Native authentication runs only after both streams close.
A verified stage is retained for review, never committed by the document
adapter; failed or cancelled imports are abandoned, including cancellation
while handing the verified result back to its caller. The 29 transfer, import
and deletion tests are host evidence; picker contracts are also registered in
the Chromium shell's Robolectric suite. Native coordinator integration and a
document-provider device flow remain separate verification requirements.

`BackupRecoveryKeyDialog` owns the explicit keep-key and enter-key ceremony.
Its `BackupRecoveryKeySession` is native-operation scoped; only
`BackupRecoveryKeyField`, a trusted Android view inside the secure Compose
dialog, receives mutable key text. The view does not save itself, take part in
autofill or content capture, or write to the clipboard. Keyboard non-learning
is requested. Temporary key arrays and the editable view are cleared after
use, on dismissal and when the window backgrounds. Successful confirmation
retains native authority for the caller's next explicit step; cancellation
discards it. This is source implementation, not device or security-review
evidence.

`BackupScreen` now connects SCR-704 to the regular window's `BackupWindowHost`
through the shell's Dagger view-model factory, on compact and two-pane Settings
layouts. `BackupViewModel` owns only the local selection and UI ceremony. It
keeps its native session private and unsaved; key text never enters Compose
state, and document URIs never enter the route or the product's saved state.
A picker request is claimed once and every result is matched to its original session. An
unlaunched operation is withdrawn on backgrounding; an explicitly confirmed
Android document handoff may finish across the picker pause, but screen or
window disposal still withdraws it.

The window-owned `BackupDocumentPicker` gives each screen a disposable
attachment. Only one opaque outstanding registry token may survive Android
recreation; it carries no native authority. The next owner consumes old
results through a drop-only registration before allowing another launch.
This also removes raw results AndroidX may have retained while no callback
was registered. If that old result has not returned, a new request remains
unavailable rather than replacing it.

The source-written screen exports the six supported SQL record classes and
checks an existing archive with its recovery key. It explicitly says that
checking is not restoring. Its check-only action abandons the authenticated
native stage instead of leaving an unconnected restore review holding it.
In 1.0 it offers neither Restore nor the review of an interrupted restore:
`BackupUiState.restoreOffered` is false and nothing sets it, because a restore
creates the profile it restores into and Chromium on Android builds only its
first profile (decision
0255,
OD-132). The restore machinery stays
compiled and tested behind that one field.
Bookmark and presentation-preference selection, restore preview/confirmation,
candidate review and provider-copy deletion are not connected to this screen.
The 21 new local presentation tests, eight Android main-loop/transfer tests
and twelve result/saved-state registry tests are source only: no compilation,
UI execution or device evidence is claimed.

Retiring a restored AndroidX launch marker reads implementation-specific keys
from its public saved-state snapshot and dispatches only a drop-only result,
after the old real result has been consumed. The original token and unique
request code must match; ambiguity keeps the request unavailable. This seam
needs rechecking when the pinned AndroidX implementation changes. It cannot
restore a session, pass a document to a new request or modify another
consumer's registry state.

Compose never sees an authorization URI, code, verifier, token, capability,
profile path, or service generation. Task, workspace, session, preferences and
provider state are immutable projections from browser/Rust ports. No protocol
logic and no network traffic live in this module — which is why the account
plane's removal (decision
0200) took
nothing out of it except an import.

Gradle is a compile-and-test aggregation of the canonical UI sources. Its
manifest deliberately declares no Activity, intent filter, permission, or
custom `Application`, so it cannot become a second browser runtime. The
shipping APK is built at `//taffy/app/android:taffy_public_apk`.

Gradle debug output uses AGP's normal local debug identity. Release output is
unsigned; no key, password, fallback credential, or product signing identity
is stored here. Product signing is external to this module.

The module holds no Firebase runtime or Google Services plugin. Future
content-free observability remains gated by
OD-047 and OD-048; a product registration
must be created for the shipping package and signing identity only when those
decisions authorize the runtime.
