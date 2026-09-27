# Golden message: `snapshot-request-child-frame`

**Message:** `SnapshotRequest` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-request-child-frame.json`](./snapshot-request-child-frame.json)

What this document proves:

- `root_frame_id` and `observed_frame_id` are different values, which is the
  only shape in which "this endpoint is not the root of the observation" is
  expressible. Every other request in this corpus is addressed to the root, so
  without this one the field would be present in the schema and never exercised
  by a document.
- The endpoint receiving it is a child frame and knows so **because it was
  told**. It cannot work this out for itself: the frame identity a renderer
  holds for its own document is one it minted in a namespace the broker does
  not share, so comparing that against `root_frame_id` compares nothing —
  it answers "different" for the main frame of a first-party page exactly as
  readily as it does here.
- `allowed_origins` names the child's own origin rather than the root's. A
  cross-origin child frame enters a task because the grant named its origin,
  never because it happened to be in the frame tree.
- `include_child_frames` is false. A request addressed to a child does not
  then descend further; each endpoint answers for its own document, which is
  what makes one snapshot describe exactly one frame.
