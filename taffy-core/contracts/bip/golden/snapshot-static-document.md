# Golden message: `snapshot-static-document`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-static-document.json`](./snapshot-static-document.json)

What this document proves:

- The baseline read: a heading, a paragraph, and a link, with `CONTAINS` and `LABELS` edges and per-field evidence naming the source of each value.
- Nothing is truncated and nothing is redacted, so a later comparison can attribute any difference to the page rather than to a budget.
- The link carries `actions` (what the adapter believes could be attempted) and a normalized `destination`. Neither is authorization: acting on this node still needs a capability, an actor lease, and matching preconditions.
