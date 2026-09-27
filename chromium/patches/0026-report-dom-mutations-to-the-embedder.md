# 0026 — Report DOM mutations to the embedder

**Status:** `[Current]` — committed on `taffy/patched` and exported by
`./tools/chromium/export-patches` on 2026-08-21 as
`0026-report-DOM-mutations-to-the-embedder.patch`.
**Needed by:** WP-M2-05 — the renderer half of deltas and backpressure. Without
this signal a delta stream is opened, granted a budget, and then delivers
nothing while the page rewrites itself.
**Estimated size:** ~2 modified upstream lines in 2 existing files, plus 3
new downstream-owned files under `third_party/blink/`

## Upstream files and symbols

| | |
|---|---|
| New file | `//third_party/blink/public/web/web_dom_mutation_observer.h` |
| New file | `//third_party/blink/renderer/core/exported/web_dom_mutation_observer_impl.h` |
| New file | `//third_party/blink/renderer/core/exported/web_dom_mutation_observer_impl.cc` |
| Edited | `//third_party/blink/public/BUILD.gn` — one entry in `blink_headers`'s sorted `sources` |
| Edited | `//third_party/blink/renderer/core/exported/build.gni` — two entries in `blink_core_sources_exported` |

## The gap this closes

`content::RenderFrameObserver` has no DOM mutation callback at the pinned
milestone, and neither does anything else an embedder can reach. Run the query
before believing the sentence:

```bash
grep -n 'virtual ' content/public/renderer/render_frame_observer.h
grep -rn Mutation third_party/blink/public/web/
```

The first prints the whole vocabulary a `//components` renderer component is
offered, and the nearest three entries to "the document changed" are
`DidChangeScrollOffset`, `FocusedElementChanged` and `DidObserveLayoutShift`.
The second prints nothing.

Those three are exactly the three signals
`//taffy/renderer/taffy_render_frame_observer.cc` forwards to
`PageIntelligenceEndpoint::OnDocumentMutated`, and they were the only callers
it had. So:

- `document.body.appendChild(p)` shifts no existing layout, moves no focus and
  scrolls nothing, and therefore advanced no graph revision and produced no
  delta;
- `PageIntelligenceEndpoint::OnNodesRemoved` — the path that retires a node id
  permanently, which is what makes a stale handle fail closed — had **no
  production caller at all**, only a test;
- protocol section 5.3 says the graph revision advances on node
  removal, reparenting and replacement, and none of those could be observed.

Two `taffy_browsertests` cases measure the hole directly:
`DeltaStreamTest.OneDeltaArrivesAndApplies` and
`DeltaStreamTest.AnAcknowledgedStreamKeepsDelivering`. A repeating
`takeRecords` drain was tried and withdrawn: it delivered those two cases on
a physical phone, then left the renderer never-idle and hung `ExecJs` on
popup and cross-origin frames. Delivery is `MutationObserver::Deliver` on
`documentElement`, and the overlay numbers the stream from 1 so the first
delta is not a sequence gap.

## Why the overlay cannot host it

`//taffy/renderer/DEPS` grants `+third_party/blink/public` and
nothing below it, which is the correct layering and is not the obstacle by
itself. The obstacle is that the mechanism only exists below that line:
`MutationObserver::Delegate` is declared in
`third_party/blink/renderer/core/dom/mutation_observer.h` and is reachable only
from inside Blink. Anything outside has to be handed a bridge, and a bridge for
a Blink type has to be compiled in Blink.

The alternatives were considered and rejected on their merits rather than on
effort:

| Alternative | Why not |
|---|---|
| `blink::WebContentCaptureClient` | Real, public, and the wrong signal: on-screen **text** only, delivered from a best-effort idle task after a client-chosen delay. An action precondition cannot be checked against it, and only one client per frame may exist — Chrome's own `ContentCaptureSender` already claims it in a full browser |
| `blink::WebAutofillClient::DidChangeFormRelatedElementDynamically` | Form-related elements only, and `AutofillAgent` already owns that client |
| `WebNode::AddEventListener` with a new `EventType` | A smaller edit, but there is no event to listen to: Mutation Events were removed upstream, and `grep -rn DOMSubtreeModified third_party/blink/renderer/core/` returns two comments |
| A `MutationObserver` installed in an isolated world from script | Works, but makes a core protocol signal depend on script execution inside the observed document, and buys nothing that this does not |

## The change

One public interface, in the shape `WebFormElementObserver` already
established for the narrower question "tell me when this form element goes
away":

```cpp
blink::WebDomMutationObserver* observer =
    blink::WebDomMutationObserver::Create(frame->GetDocument(), callback);
// ... later, exactly once:
observer->Disconnect();
```

The implementation is a `MutationObserver::Delegate` registered on
`document.documentElement()` (falling back to the `Document` node when the
element does not exist yet) for `childList`, `attributes` and `characterData`
with `subtree`, and a `SelfKeepAlive` released by `Disconnect()` — the same
lifetime rule `web_form_element_observer_impl.cc` uses.

`Create` returns null when the document has no execution context. That is the
initial empty document of a `window.open`, not a loaded page, and returning
null is the honest answer rather than CHECKing: a CHECK here crashes the
renderer the moment a page opens a popup.

`FlushPending()` drains `takeRecords()` into the callback. The overlay calls
it only while a delta subscriber is listening, so a page that is not
compositing still produces a stream, without a repeating timer on every
observed document.

`WebDomMutation` names nodes by `blink::WebNode::GetDomNodeId()` rather than by
`WebNode`, so a consumer matches a mutation against records it keyed off
`GetDomNodeId()` without holding a reference into the Blink heap. That is the
same integer `//taffy/renderer/adapters/dom_adapter.cc` already
keys its identity map on, so the two agree by construction rather than by
convention.

Character data changes report `parent_dom_node_id` beside the target because
the target of such a record is a text node, and no adapter ever names a text
node. Without the parent the signal would arrive with nothing to attribute it
to.

## What it deliberately does not do

- **It classifies nothing.** Which change class a mutation belongs to, whether
  it is droppable, and which node it is about are all decided downstream in
  `//taffy/renderer/dom_mutation_signal_source.cc`, where they can
  be unit-tested on a host with no renderer. A bridge that made those
  decisions would be policy living in the fork.
- **It costs nothing until asked.** No observer exists unless an embedder
  creates one, so a build that never calls `Create` pays only the compile.

## Rebase risk

Low, and the reason is worth stating rather than asserting: the three new files
are downstream-owned, so a rebase can only conflict on the two sorted build
lists. The Blink API they stand on — `MutationObserver::Create(Delegate*)`,
`MutationObserverInit`, `MutationRecord`, `Node::GetDomNodeId()` — is the same
API `web_form_element_observer_impl.cc` uses two files away, so a signature
change upstream breaks upstream's own file at the same moment and cannot land
silently.

The one thing to re-check at a milestone rebase is
`third_party/blink/renderer/core/exported/build.gni`: if upstream reorganises
`blink_core_sources_exported`, the two added entries move with it.

## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-21, from the
`Taffy-Patch: 0026` commit on `taffy/patched`. The hand-assembled export this
section used to describe, and the debt it recorded — a queue file whose five
files existed only as uncommitted state in the checkout, one `sync` away from
being discarded — were retired the same day: the files were committed with
the trailer and the exporter rewrote the queue from the commit (the file is
now `0026-report-DOM-mutations-to-the-embedder.patch`, named from the commit
subject). The branch and the queue agree, and `./tools/check fast --only
chromium` verifies that agreement together with the fork-debt figures.
