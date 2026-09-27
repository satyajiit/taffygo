# Golden message: `snapshot-conflicting-jsonld-price`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-conflicting-jsonld-price.json`](./snapshot-conflicting-jsonld-price.json)

What this document proves:

- Two sources disagree about a price and both survive. The visible document value and the structured-metadata value are separate candidate nodes, each with its own source, confidence, and evidence locator.
- The conflict is marked rather than resolved: the metadata adapter reports `CONFLICTED`, the snapshot carries a `CONFLICTING_EVIDENCE` warning, and the two candidates are joined by an inferred `SAME_ENTITY_AS` edge.
- No adapter fabricates a single normalized answer to satisfy the requested shape. Choosing between candidates is a later decision made with the provenance in hand.
