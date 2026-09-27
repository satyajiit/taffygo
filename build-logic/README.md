# `build-logic`

**Status:** `[Current]` The convention plugins, and the three checks that make
the architecture rules executable.

Owning milestone: M0, work package
WP-M0-07.

Authoritative specifications:
android-app-architecture.md
section 5; decision
0014;
[TOOLCHAIN.md](../TOOLCHAIN.md) for every pinned version, which lives there and
is referenced here rather than restated.

## The authority boundary

**Shared build configuration lives here and nowhere else.** A module's build
script names its plugin and its namespace; everything else — the compile and
minimum platform levels, the Java version, the Kotlin settings, the lint
configuration, the test dependencies, the Compose setup, the dependency injection
setup — is decided once in a convention plugin. Convention plugins add only
toolchain and external-library dependencies; they never inject a catch-all set
of project modules. Exact direct project edges come from
`taffy-core/build/components.toml` through its generated Gradle projection.

Three rules of the architecture are checks that run in the build rather than
notes a reviewer might catch. Each is attached to `preBuild`, `classes` and
`check`, so it runs in the assemble path, the lint path, and the test path alike:

- **`checkModuleGraph`** holds the declared layering. Every module is in exactly
  one layer, a module may depend only on a lower layer, no feature may depend on
  a feature, and a module path that is not in the declared graph is itself a
  finding — so adding a module without declaring where it sits fails the build;
- **`checkFileDiscipline`** holds one public type per file and the soft line cap;
- **`checkApiSurface`** holds api and implementation separation: a declaration in
  an internal package is internal, no module reaches into another module's
  internal package, and a core module that publishes an interface has both an api
  package and an internal one;
- **`checkStringResources`** holds parity row PAR-L10N-001: no user-visible
  literal in a source file, no duplicate name, no reference to a name the module
  does not declare, and no declared name left unused.

## What this module deliberately does not do

It pins no version. Every version comes from the catalog and from
[TOOLCHAIN.md](../TOOLCHAIN.md); a convention plugin reads them and never writes
them. It publishes nothing and is not consumed outside this build.

It applies no Kotlin plugin to an Android module. From Android Gradle plugin 9,
Kotlin is built in, and applying the standalone Android Kotlin plugin is a hard
configuration error. Pure Java-virtual-machine modules still apply the standalone
Kotlin plugin, which is why two conventions exist rather than one.

## What each plugin owns

| Plugin | Owns |
|---|---|
| `taffygo.android.application` | The shell: identity, platform levels, packaging, lint |
| `taffygo.android.library` | Every Android module: platform levels, lint, the four checks |
| `taffygo.android.compose` | Compose, its bill of materials, and its test dependencies |
| `taffygo.android.feature` | Android-library and Compose conventions for a feature; no project edges |
| `taffygo.jvm.library` | Pure Java-virtual-machine modules, and lint for them too |
| `taffygo.dagger` | Regular Dagger runtime plus its KSP component processor |
| `taffygo.test` | The test contract every module shares, and the fast lane's task name |
