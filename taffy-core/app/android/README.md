# Android product assembly

**Status:** `[Current]` The Android product is assembled directly at
`//taffy/app/android:taffy_public_apk`.

This directory owns Android product packaging and the narrow platform seams
that Compose cannot provide itself. It does not own task, policy, account,
workspace, preference, credential, or storage state.

## Composition and lifetime

Regular Dagger follows Chromium's actual owners:

```text
TaffyProcessComponent
├── TaffyProfileComponent (regular)
│   ├── TaffyWindowComponent
│   └── TaffyTabComponent
└── TaffyPrivateProfileComponent
    └── TaffyTabComponent
```

Window and Tab are siblings because a tab may move between windows. The
browser-process provider creates the Process root once from the product's
post-native-initialization hook, and a Chromium `ProfileKeyedMap` owns one
type-specific regular or private Profile root. A private root is selected
before the runtime constructs any Core API endpoint, profile ports, account
adapter, preference store, secure store, platform surface, or Window builder.
Private tabs coexist in their selector's regular Window and retain their own
ephemeral profile and Tab lifetimes. Android has no reliable process-shutdown
callback, so the Process root has the exact OS process lifetime and holds only
application-context bindings; Profile, Window, and Tab roots still close
explicitly through their Chromium owners. A
`TaffyBrowserActivity` opens and closes only its Window component. A
profile-owned Tab observer keeps the Tab component through window moves,
rebuilds it only when the live WebContents changes, and closes it on Tab or
profile destruction. Each component exposes one lifetime object; its owner
closes it exactly once.

`shell/host/TaffyProfileRuntimeProvider` is the only Java-facing profile lookup.
It returns browser-backed ports and never constructs a default, preview, or
in-memory product graph. A missing profile runtime is a product error.

Every Activity selector is created through Chromium's `TabWindowManager`.
The manager's assigned window id is the single persistence identity used by
the tab-state policy and store. Saved Activity state requests that id again on
rotation; a new Activity requests the first free id; the upstream mismatch
protocol transfers an id only from the Activity it is replacing.

## Boundaries

- Compose owns rendering, navigation projection, platform permission prompts,
  Credential Manager presentation, Custom Tabs, and redirect delivery.
- The trusted browser callback adapter validates redirect structure, stores an
  authorization code immediately, and forwards only an opaque handle and
  returned state through Core API. Compose never observes the URI or code.
- Task, account, workspace, and preference commands and projections cross the
  generated Core API. No Kotlin reducer is authoritative for them.
- Browser navigation and tab handles cross the Window-scoped browser port.
- Page observations cross the browser-owned page-intelligence port.
- The sandboxed Rust service is reached through `//taffy/browser`; Android does
  not link a Rust runtime or expose a JNI task side channel.

## Build graph

- `BUILD.gn` owns page-host, Compose-host, and aggregate Java targets.
- `ui/BUILD.gn` compiles the canonical sources under `taffy-core/ui/android`.
- `shell/BUILD.gn` owns the Chromium activity and tab-model adapter.
- `product_targets.gni` defines the final APK target directly in this directory.
- `taffy_dagger_library.gni` runs Dagger's component processor for GN targets.

`tools/kotlin_mounts.py --mount` materialises generated symlinks under
`app/android/kotlin/`; `--verify` proves that every mounted source still points
at its canonical Gradle source and that product exclusions remain excluded.
The links are gitignored host state: a module added to the contract has no
link on another host until `--mount` runs there, and `./tools/bootstrap`
runs it for the `android` and `chromium` profiles.

## Product boundary

The shipping APK uses the browser-owned task, profile, tab, storage, and
network services. Protocol fixtures and deterministic test ports live only
under test source sets.

## Focused verification

```bash
python3 taffy-core/app/android/tools/kotlin_mounts.py --mount   # host state; idempotent
python3 taffy-core/app/android/tools/kotlin_mounts.py --verify
./gradlew --no-daemon :app:compileDebugKotlin
python3 taffy-core/app/android/tools/dagger_contract.py --self-test
```

In a mounted Chromium checkout, verify the final label with:

```bash
gn desc out/dev-arm64 //taffy/app/android:taffy_public_apk deps --all
```
