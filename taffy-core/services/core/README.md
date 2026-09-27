# Isolated core service

This directory owns the utility-process adapter for TaffyGo's profile-scoped
Rust core. It is the only shipping target that composes `task-engine`,
`policy-engine`, `audit-engine`, `model-router`, `storage-domain`, and
`core-runtime`.

The service is launched lazily by `//taffy/browser:core_service_client`. A
regular profile and every off-the-record profile receive separate service
instances and generations. Mojo control, cancellation, and disconnect handling
stay on the utility main sequence. `CoreServiceImpl` owns one
`base::SequenceBound<RustCore>` on a sequenced runner, so ordered Rust state is
never entered concurrently. Chromium owns tasks, clocks, shutdown, and
cancellation; this service has no Tokio runtime.

## Authority boundary

The service receives generated Core Service values only. It never receives a
profile path, SQL handle, filesystem capability, browser object, URL loader,
cookie jar, credential, provider key, or platform UI object. Rust proposes
typed effects. The browser owns physical storage, network, tabs, actor leases,
capability narrowing and spending, credentials, and tool-process launch.

`STORAGE_COMMIT` is the one durability gate. Its generated transaction batch
contains the reducer command, events, independent audit projection, and effect
intents. The browser storage writer commits that batch atomically at the
expected revision. `CoreEffectBroker` deliberately bypasses the generic effect
intent/result journal for `STORAGE_COMMIT`; wrapping it would create a second
journal and ambiguous crash semantics. Other effects follow:

```text
durable effect intent -> broker dispatch -> durable terminal result -> core
```

Only a successful storage completion releases reducer effects. A failed or
ambiguous completion marks the task for replay and releases nothing.

A deadline is the third way that wait can end. Every deadline here is
browser-owned and monotonic, and nothing below this boundary may read a clock,
so the core notices an arrived deadline only when a call hands it a later
`now` — and a commit whose completion never arrives is exactly the reason no
other call arrives either. `CoreServiceImpl` therefore runs one
`base::RepeatingTimer` while the service is ready and calls
`RustCore::ExpireDueOperations`, which has the shape of every other command:
the time goes in as an argument, and the `BridgeResponse` that comes back is
published through the one batch path, so an operation ended by its deadline
reaches the browser exactly as one ended by a delivered failure does.
`core_service_impl_deadline.cc` holds the interval and the argument for it.

## Source and dependency ownership

`crate_sources.gni` is generated from the component-owned Cargo crates. There
are no source copies or crate mounts in this directory. `service_bridge.rs` is
the sole hand-written Rust/C++ adapter and `rust_core.*` is the sole C++ owner
of that bridge. `BUILD.gn` exposes only `:service` to `//taffy/utility`; no Rust
target is visible to `//taffy/browser`.

`service_bridge.rs` defines no record of its own. It names them: every struct
lives in a `service_bridge_<plane>_ffi.rs` module, and the root's
`unsafe extern "C++"` block aliases each one back so `crate::ffi::X` resolves
for the whole crate and `taffy::core_bridge::X` stays the single C++ name. A
bridge may name a record another bridge defines, because cxx emits
`ExternType<Kind = Trivial>` for every shared struct and a trivial extern
alias may be a by-value field, element or argument — the same mechanism the
root has always used to return these records by value. What that leaves to
decide is where a record's `Vec<T>` is instantiated, and the three rules below
are shaped by that:

- A record that a published `BridgeState`, a `BridgeInitialization` or a
  `BridgeResponse` holds a `Vec` of goes in `service_bridge_state_ffi.rs`.
  Defining it there instantiates the vector implicitly and once. A `Vec<T>` of
  a record another bridge defines needs `impl Vec<T> {}` in exactly one bridge
  — `Vec<BridgeTaskEffect>` is the one case, named by the state bridge — and
  a second bridge naming the same vector emits the `VecElement` impl and the
  C++ `rust::Vec<T>` symbols twice.
- A record that is only ever a by-value field, element or argument of another
  bridge's record may live in its own module and be aliased in through
  `type X = crate::<module>::ffi::X;`. `BridgeOperation`
  (`service_bridge_operation_ffi.rs`), `BridgeTaskEffect`
  (`service_bridge_task_effect_ffi.rs`) and `BridgeEntitlementDelivery`
  (`service_bridge_entitlement_ffi.rs`) are placed this way. That was "the two
  delivery records that wrap a `BridgeResponse`, in the catalog and entitlement
  modules" until 2026-09-20: there is no catalog module, and of the three
  `Bridge*Delivery` records only the entitlement one wraps a `BridgeResponse` —
  composer and model stream deliberately do not, and composer's own module
  comment says why. Count them before trusting this sentence; the include graph
  below depends on which records carry a response.
- A record only one plane names, and no published state carries, goes in that
  plane's own module, which mirrors the operation envelope under its own name
  and converts it exactly once in the plane's projection.

The `include!` graph between the modules must be a DAG, because each generated
header includes the headers its bridge named: operation, then task effect, then
state, then the planes that wrap a `BridgeResponse`, then the root. A module
never includes the header of a module that includes it.

Each `*_ffi.rs` module needs a `cxx_bindings` entry in `BUILD.gn` beside its
`sources` entry, or its header is never generated and the root's `include!`
does not resolve.

Run the static graph checks after changing a crate or the bridge:

```bash
python3 taffy-core/services/core/tools/generate_crate_sources.py --check
python3 taffy-core/services/core/tools/check_build_graph.py
python3 taffy-core/build/tools/check_build_reachability.py
python3 taffy-core/services/core/tools/check_publication_totality.py
python3 taffy-core/services/core/tools/check_effect_withdrawal.py
```

The last one is the status-publication totality check: every bridge entry
that mutates the runtime must either reach the publication seam in
`service_bridge_status.rs` or hold a register row in the tool naming the
discipline it follows instead (commit-staging, or exempt with the reason).
The effect-withdrawal check derives every effect vector from `BridgeResponse`
and requires both failed-publication paths to clear all of them before the
browser can dispatch anything. Its exhaustive field set is a host-side proof
for the GN-only Rust bridge, not a hand-maintained list.

Cargo is the fast host loop. A Chromium checkout build is authoritative for
the shipping utility process and generated CXX/Mojo boundary.
