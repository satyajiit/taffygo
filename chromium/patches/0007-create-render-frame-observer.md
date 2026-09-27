# 0007 — Create the TaffyGo render frame observer

**Status:** `[Current]` — committed whole on `taffy/patched` and exported by
`./tools/chromium/export-patches` on 2026-08-21. The `.patch` beside this
file carries all three files, from one commit.
**Needed by:** WP-M2-03 — the renderer endpoint never runs until something
constructs it
**Estimated size:** ~7 modified upstream lines, 3 files

## Upstream files and symbols

| | | |
|---|---|---|
| File | `//chrome/renderer/BUILD.gn` | **exported** |
| Symbol | `static_library("renderer")`'s `deps`, gaining `//taffy/renderer` | |
| File | `//chrome/renderer/DEPS` | **exported** |
| Symbol | the include-rule list, gaining `+taffy/renderer` | |
| File | `//chrome/renderer/chrome_content_renderer_client.cc` | **exported** |
| Symbol | `ChromeContentRendererClient::RenderFrameCreated(content::RenderFrame*)` | |

## What the exported patch does today

`0007-create-the-TaffyGo-render-frame-observer.patch` carries three hunks: the
`deps` entry on `//taffy/renderer` in `//chrome/renderer/BUILD.gn`,
the matching `+taffy/renderer` include rule in
`//chrome/renderer/DEPS`, and the construction in
`ChromeContentRendererClient::RenderFrameCreated`.

It carried only the first of those until 2026-08-20. While that was true, **no
TaffyGo render frame observer was constructed, so no renderer frame had an
endpoint**, which is the
gap the overlay's `renderer/README.md` records.

## The change

Construct the observer beside the other `RenderFrameObserver` subclasses that
function already creates:

```cpp
new taffy::TaffyRenderFrameObserver(render_frame);
```

`DEPS` gains one scoped rule — `+taffy/renderer`, and deliberately
not the wider `+taffy` rule, because
the renderer process has no business reaching the browser half.

### The lifetime rule, confirmed rather than assumed

The bare `new` with no owner is correct, and it is worth writing down why,
because it is the one thing in this patch that looks like a leak:

* `content::RenderFrameObserver` (`content/public/renderer/render_frame_observer.h`)
  registers itself with its `RenderFrame` in its constructor and declares
  `OnDestruct()` pure virtual precisely so that every subclass has to state
  its own destruction policy.
* `taffy::TaffyRenderFrameObserver::OnDestruct()`
  (`taffy/renderer/taffy_render_frame_observer.cc`) retires the
  endpoint with `InvalidationReason::kFrameDetached` and then calls
  `delete this`.

So the object's lifetime is the frame's, the retirement of the page-intelligence
endpoint is ordered before the deletion, and the surrounding lines in
`RenderFrameCreated` use the same idiom for the same reason.

### Every frame, not main frames only

The observer is created for every `RenderFrame`. The constructor's only work is
one `AddInterface` on the frame's associated-interface registry plus an integer
increment, so the cost of the permissive order is nil, and it puts the
cross-origin child-frame refusal in
`//taffy/components/intelligence/content/frame_inclusion_policy.cc` — a file the fork owns
and unit-tests — instead of in an upstream `if` that no downstream test covers.

**This decides `[Open (OD-045)]` in the permissive direction by omission unless
`frame_inclusion_policy.cc` actually refuses.** It does today; a change that
weakens it silently widens what TaffyGo reads, and this paragraph is the record
of that coupling.

### No feature guard

`//taffy/browser/feature_posture.h` declares no `BASE_FEATURE` for
page intelligence, and adding one here would put a policy decision in an
upstream file. The observer is constructed unconditionally and every refusal
lives downstream of it.

## Why the overlay cannot host it

`RenderFrameCreated` is the embedder's one notification that a frame exists,
and `content` offers no downstream registration for additional observers. An
overlay component has nothing to attach to before this call.

## Rebase risk

**Low.** One line among a dozen identical lines. The risk is not conflict but
omission: if upstream adds a second frame-creation path, an observer created
only here would miss frames created by the other. That failure is silent from
the renderer's side and visible from the broker's, because a frame with no
endpoint fails every observation request rather than answering wrongly.

**Retirement:** permanent while page intelligence exists.

## What SP-04 asked, and the answers now in hand

1. **Is `RenderFrameCreated` still the hook, and still where the neighbouring
   observers are constructed?** Yes at the pinned milestone. The construction
   goes after `new NetErrorHelper(render_frame);` and before the
   `associated_interfaces` block, so the observer exists before anything binds
   an associated interface on the frame.
2. **Every frame or main frames only?** Every frame — see above, and the OD-045
   coupling it creates.
3. **Must anything be created earlier, at `RenderThreadStarted`?** No. The
   interface is channel-associated and served from the frame's own
   `blink::AssociatedInterfaceRegistry`, which does not exist before the frame
   does. That is the same evidence that retires patch 0006's unwritten half and
   closes OD-027; see
   decision 0032.

## Regenerating the patch

The call site belongs in the **same commit** as the build edge, which is what
the exported patch's own message promises. Fold it rather than taking a new
number:

```bash
./tools/chromium/sync
# edit the three files in the checkout, then:
git -C "$CHROMIUM_SRC" commit --fixup 742f718bc88f5
git -C "$CHROMIUM_SRC" rebase -i --autosquash "$(cat chromium/REVISION)"
./tools/chromium/export-patches
./tools/check fast
```

Take a branch or a tag before the rebase: it crosses the whole applied stack,
and a lost stack costs far more than the four lines it saves.


## How this was exported

Exported by `./tools/chromium/export-patches` on 2026-08-21, from the
`Taffy-Patch: 0007` commit on `taffy/patched`. The hand-written `git diff`
export this section used to describe, and the debt it recorded — a queue
file whose lines existed only as uncommitted modifications in the checkout —
were retired the same day: the DEPS and renderer-client edits were
squashed into the original build-edge commit, whose message now describes
the whole three-file change, and the exporter rewrote the queue from it.
The branch and the queue agree, and `./tools/check fast --only chromium`
verifies that agreement together with the fork-debt figures.
