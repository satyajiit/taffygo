# `taffy-core/ui/android`

**Status:** `[Current]` The canonical Android Compose projection for every
Taffy-owned surface. Chromium GN and Gradle compile the same source tree; there
is no preview browser, Kotlin task runtime, or second product graph.

Owning milestone: M0,
WP-M0-07. Architecture:
android-app-architecture.md,
decisions 0024,
0031, and
0038.

## Ownership

Android owns Compose widgets and Android-native surfaces: runtime permissions,
Credential Manager, Custom Tabs, callback intent delivery, system bars,
locales, and Activity/Window integration. It does not own task, workspace,
account, session, provider-route, or preference business state.

Those values arrive as immutable browser/Rust projections through generated
Core API semantics. Commands are UI intents only. The browser stamps operation
generation, revision, deadline, identity, and authority; Compose cannot mint or
spend a capability. Credential and authorization-code material is exchanged
immediately for an opaque profile-bound secure-store handle and never enters a
screen state.

This Android tree has never held a network client for an account or a
provider, and that is what made the account plane's removal a deletion rather
than an excavation: PKCE, sessions, refresh, provider protocol and credential
persistence belong to Rust and the browser brokers, so nothing here had to be
unpicked when decision
0200 removed
the plane those brokers talked to.

## Lifetimes

Regular Dagger mirrors Chromium ownership exactly:

```text
TaffyProcessComponent
├── TaffyProfileComponent (regular)
│   ├── TaffyWindowComponent
│   └── TaffyTabComponent
└── TaffyPrivateProfileComponent
    └── TaffyTabComponent
```

Window and Tab are siblings because a tab may move between windows. The
browser process starts its Process component only after Chromium native
initialization, and a Chromium profile-keyed map owns one type-specific graph
per regular or private profile. A private graph owns only its ephemeral
profile lifetime and movable Tab components; it cannot construct a Window or
reach account, preference, credential, secure-material, or persistent-storage
bindings. Android exposes no reliable process-shutdown
callback, so only that application-context-only Process root ends with the OS
process; Profile, Window, and Tab lifetimes close exactly once through their
Chromium owners. An Activity owns its Window; a profile-owned observer binds
each Tab component to the Tab's current WebContents and does not rebuild it for
a window move. No Application context contains an active-profile slot.

View models use Dagger-assisted factories contributed through multibindings;
there is no service locator or central class-literal switch.

## Modules and dependency authority

`taffy-core/build/components.toml` is the sole source of module registration,
API/implementation edges, platform, process, and trust-domain metadata. It
generates Gradle settings and exact dependency sets as well as GN graph
fragments. Module build scripts contain only module-local Android configuration
and external libraries.

The 23 modules are:

- `:app`
- `:core:analytics`, `:core:api`, `:core:assets`, `:core:browser`,
  `:core:common`, `:core:credentials`, `:core:designsystem`, `:core:model`,
  `:core:page-intelligence`, `:core:preferences`, `:core:providerauth`,
  `:core:providers`, `:core:task`, `:core:ui`, and `:core:workspace`
- `:feature:assistant`, `:feature:browsing`, `:feature:downloads`,
  `:feature:onboarding`, `:feature:providers`, `:feature:settings`, and
  `:feature:workspaces`

`taffy-core/build/generated/android-modules.tsv` is the list this sentence
paraphrases, and it is generated from `taffy-core/build/components.toml`; read
it rather than this paragraph when the two disagree.

Every feature receives only the ports named by its exact generated direct
edges; features never depend on other features. The shell is the only module
that composes multiple feature screens.

## Screens

The preserved Compose surfaces cover onboarding, sign-in, AI setup, browser
chrome, tabs, downloads, Assistant bar, task preview and progress, workspaces,
sources, fact correction, export, provider settings, notifications, and
appearance. Their catalog identifiers and copy remain authoritative in
screen-catalog.md and
ux-spec.md.

Each screen keeps the same form: immutable `UiState`, sealed `Intent`, a
`StateFlow` view model, and a pure projection or reducer that host tests can
exercise without a device. Protocol fixture clients and seeded browser state
exist only under test source sets and cannot enter the product APK. Compose
preview annotations, fixed states, and preview functions live only under each
module's `src/debug/`; release variants and the GN product graph compile only
the retained shipping surfaces.

## Build and verification

The Gradle `:app` module is a compile-and-test aggregation. Its manifest has no
Activity, browser intent filter, permission, or custom Application, so it
cannot become a second runtime. The shipping APK is
`//taffy/app/android:taffy_public_apk`.

From the repository root:

```bash
./gradlew --no-daemon lintDebug testDebugUnitTest
python3 taffy-core/app/android/tools/kotlin_mounts.py --verify
python3 taffy-core/app/android/tools/generate_kotlin_sources.py --check
python3 taffy-core/ui/android/tools/icons/generate_launcher_icons.py --check
```

`checkModuleGraph`, file discipline, API-surface rules, and string-resource
checks run with the Gradle lifecycle. Compose instrumentation suites require an
Android device or emulator and run through `connectedDebugAndroidTest`.
