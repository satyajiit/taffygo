# Golden message: `snapshot-oopif-frame-tree`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-oopif-frame-tree.json`](./snapshot-oopif-frame-tree.json)

What this document proves:

- A frame tree assembled by the browser process, not by a renderer: a main frame plus two cross-origin children, each with its own origin, page epoch, and graph revision.
- The embedded widget frame is out of process and included; nodes carry its own `frame_id` and an edge crosses the boundary with both endpoints labelled.
- The hidden third-party frame is present in the tree but excluded, with `HIDDEN_THIRD_PARTY` as the reason and a matching warning. A frame is not observed merely because it exists; which cross-origin child content may enter a task is [Open (OD-045)].
