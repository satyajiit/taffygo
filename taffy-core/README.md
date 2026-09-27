# `//taffy`

`taffy-core/` is TaffyGo's first-party browser product root. The Chromium
checkout mounts this directory at `src/taffy`, so its GN label is `//taffy`.
There is no compatibility mount or forwarding label under
`//components/taffy`: a stale dependency must fail instead of silently keeping
the retired layout alive.

This root contains browser-owned code only. The managed AI Worker, account
plane, website, fixture corpora, repository commands, Chromium pin, and patch
queue remain at the repository root.

## Component graph

[`build/components.toml`](build/components.toml) is the machine-owned component
register. Every row records:

- the process and trust boundary;
- the platforms for which the component is designed;
- its owned source roots and exact direct dependencies;
- whether it is test-only; and
- whether it is active in the build or still planned.

`planned` is not a build claim. It reserves a checked dependency seam without
creating a GN or Gradle target. An `active` component must own files on disk and
may depend only on other active components. This makes activation proceed from
the portable leaves upward and prevents an assembly target from getting ahead
of its implementation.

The register is checked and projected with the standard-library-only tool:

```bash
python3 taffy-core/build/component_graph.py --check
python3 taffy-core/build/component_graph.py --self-test
python3 taffy-core/build/component_graph.py --format gn
python3 taffy-core/build/component_graph.py --format gradle
python3 taffy-core/build/component_gn_graph.py
python3 taffy-core/build/component_gn_graph.py --self-test
```

Generated projections are committed under `build/generated/` and consumed by
both GN policy checks and Gradle settings/dependency injection. `--check`
refuses any hand-maintained drift; nobody may keep a second dependency list.

The GN graph check owns exact target reachability and exact first-party
boundaries. A component may own several targets; the checker unions the direct
component edges of every non-test target it owns, treats source and generated
action input ownership as an edge, and inherits ownership through GN-generated
helper targets. Unknown dependency/source expressions fail closed. Its static
mode also discovers every non-generated `.cc`, `.m`, and `.mm` file from disk
first and rejects an implementation that no parsed GN target names in
`sources` or `inputs`; a forgotten translation unit therefore cannot hide
behind an otherwise valid target graph. Generated paths and files carrying the
opening `@generated` marker are excluded. Static mode runs on every host.
After `gn gen`, the Chromium build command also checks the configured graph for
that output's target platform:

```bash
python3 taffy-core/build/component_gn_graph.py \
  --checkout /path/to/chromium/src \
  --gn-out /path/to/chromium/src/out/profile
```

## Root build file

[`BUILD.gn`](BUILD.gn) owns the native/product aggregates and the real
`taffy_unittests` and `taffy_browsertests` binaries. The installable Android
entry remains the source-owning
`//taffy/app/android:taffy_public_apk`; there is no retired-path forwarding
target.
