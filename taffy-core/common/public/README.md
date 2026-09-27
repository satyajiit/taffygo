# `//taffy/common/public`

**Status:** `[Proposed]` implementation of the contract in
the protocol specification
and decision 0004
**Implementation status:** `[Current]` **compiled.** It is header-only, and
the first thing the Chromium track had to prove about it — that it compiles
with no dependencies at all — now holds at the pinned milestone:
`//taffy/common/public:public` is in the `taffy_public_apk` and
`taffy_unittests` graphs and builds clean for `dev-x64`, `dev-arm64` and
`release-arm64`. Nothing here has behaviour to test, so a compile is the whole
of the evidence this directory can have; what these declarations *mean* is
proved in `//taffy/browser`, where the definitions live. See
chromium fork and build
section 6 for the run, on a developer workstation rather than a builder that
owns the evidence of record (Open (OD-026)).
**Owning milestone:** M2 (page intelligence), work packages WP-M2-02, WP-M2-05
and WP-M2-07.
**Wire contract:** [taffy-core/contracts/bip](../../contracts/bip/README.md).
The schema wins on any disagreement; these headers are its owned-value-type
projection into C++, and `//taffy/contracts/bip/mojom` is its Mojo projection.

## The authority boundary

This directory is the **process-neutral browser API** used by the browser-side
Core Service adapter and by the Java/Kotlin facade in `//taffy/app/android`.
The sandboxed Rust core receives generated Core Service types, not browser
objects or this C++ interface.

It declares. It decides nothing. Every type here is a value or an interface;
the definitions, and therefore every decision, live in
`//taffy/browser`. A reader looking for where a capability is
consumed, an epoch is assigned, or a postcondition is verified will not find it
here, and that is the point: a caller cannot reach past this surface into a
Chromium object.

Three rules follow, and `BUILD.gn`'s empty `deps` list plus `DEPS`'s explicit
denials are how they are enforced rather than remembered:

1. **Owned value types only.** No Chromium raw pointer, no `WebContents`, no
   `RenderFrameHost`, no `GURL`, no `url::Origin`, no `base::Time`, and no
   borrowed reference crosses this boundary in either direction.
2. **Mechanical mirrors.** Every type is an aggregate of fixed-width integers,
   enumerations with an explicit underlying type, `std::string`, `std::vector`
   and `std::optional`, so the `cxx` shim is a mirror rather than a
   hand-written serializer.
3. **Identifiers are opaque.** A caller may compare one and hand it back. It
   may not derive one, order one, or infer which Chromium object it names.

## The files

| File | Owns |
|---|---|
| `bip_identity.h` | The identity hierarchy of protocol section 5: profile, window, tab, frame, page epoch, graph revision, semantic node, and the full `NodeHandle` |
| `bip_budget.h` | Observation budgets, the process ceiling, and the policy grant a request is clamped against (protocol section 7.1) |
| `bip_action.h` | The action contract of protocol sections 11.2 to 11.7: proposal, envelope, preconditions, postconditions, result |
| `bip_observation.h` | Requests and snapshot envelopes (protocol section 7), the task/direct-intent observation authority tag retained only in browser audit, and the invalidation notice of protocol section 13 |
| `bip_delta.h` | Incremental updates, the applicability rule, and the drop order (protocol section 10) |
| `bip_subscription.h` | Subscription lifecycle and backpressure (protocol section 10) |
| `bip_protocol_support.h` | What one endpoint says it supports (protocol section 6.2) |
| `bip_result.h` | The two result taxonomies, and the predicates code is allowed to reason with |
| `page_intelligence_service.h` | The service and its result sink: one terminal result per request, always |
| `taffy_download_intent.h`, `taffy_product_identity.h` | The milestone-M1 product seams |

## Two properties worth checking in review

Both are compile errors rather than conventions, which is why they are here
rather than in a checklist.

1. **Renderer acknowledgement cannot become verification.** `ActionResultCode`
   has exactly one success member and `IsActionSuccess` is a function rather
   than a comparison, so adding a code cannot widen success by accident. The
   evidence types the verifier reads live in `//taffy/browser` and
   have no field for what a renderer said.
2. **A protected delta class cannot be dropped.** `SheddableDeltaClass` has no
   public constructor, and its factory returns `std::nullopt` for
   `kNodeRemoved` and `kLifecycle`. An interface that sheds work takes that
   type, so shedding a node removal is not a defect review has to catch — it is
   a program that does not compile.

## What this directory deliberately does not do

- **No raw BIP semantic graph as C++ structs.** A snapshot's and a delta's nodes cross
  as an opaque encoded payload that the isolated Rust core decodes. Parsing untrusted,
  unbounded structure in the browser process is the work the Rule of Two says
  must not happen in unsafe code, and moving it into Rust is the point of
  decision 0004. The one exception, `ResolvedNodeFacts`, is a handful of
  scalars the browser has to re-read to decide whether an action is still
  legal, and it is small on purpose. The retained Page Inspector is a separate
  browser-produced, bounded, redacted UI projection with fresh identifiers; it
  is not a decoded BIP graph and its records never travel back to the renderer
  or into the core service.
- **No policy.** What a task may observe arrives as an `ObservationPolicyGrant`
  that the Rust policy engine decided. Nothing here can widen one.
- **No numbers.** `ProcessBudgetLimits` declares the field set and owns no
  values; the provisional values live in exactly one place,
  `browser/budget_clamp.cc`, and are `[Open (OD-031)]`.
- **No threading.** Every method of every interface here is called on the
  browser UI thread. Core Service Mojo and the browser effect broker own the
  process and sequence hops.
