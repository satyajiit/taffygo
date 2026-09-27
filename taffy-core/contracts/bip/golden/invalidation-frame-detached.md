# Golden message: `invalidation-frame-detached`

**Message:** `PageInvalidation` from [`delta.schema.json`](../schema/delta.schema.json)  
**Document:** [`invalidation-frame-detached.json`](./invalidation-frame-detached.json)

What this document proves:

- A frame leaving the frame tree is nameable as itself. `FRAME_DETACHED` was added at protocol 0.3 because every reason that already existed would have misdescribed it: `ENDPOINT_DISCONNECTED` says the pipe was lost, `RENDERER_CRASHED` says the process died, and `TAB_CLOSED` is simply false for a subframe. A subscriber that cannot tell a routine subframe navigation from a dead renderer cannot report the difference to a user or to a log.
- The page epoch survives. `retires_page_epoch` is false: the document that owns the epoch is still there, and handles bound to the parent frame remain valid. Retiring the epoch here would throw away a correct projection of the rest of the page.
- The death is scoped to the named frame. `invalidates_child_frames_only` is true, which is what makes this narrower than a navigation: handles inside `frame_child_ad` are dead, and nothing above it is affected.
- No resnapshot is owed. `resnapshot_required` is false, and that is the point of naming the reason precisely — the subscriber already knows exactly which handles died, so it can drop them and keep the rest. A reason that only said "something was disconnected" would have forced a full re-read of a page that did not change.
- No replacement epoch is named. `new_page_epoch` is absent because none is needed: nothing was rebound, the frame is simply gone.

Compare [`invalidation-cross-document-commit`](./invalidation-cross-document-commit.md), which is the opposite shape in all four flags — the epoch retires, the scope is the whole page, a resnapshot is required, and a replacement epoch is named.
