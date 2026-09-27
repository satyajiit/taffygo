# Golden message: `snapshot-selection-and-layout-reports`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-selection-and-layout-reports.json`](./snapshot-selection-and-layout-reports.json)

What this document proves:

- The selection adapter and the layout adapter carry their own `AdapterReport`
  rows, named `SELECTION` and `LAYOUT`. Before the minor step recorded in
  [`bip.version.json`](../schema/bip.version.json) neither could be named at
  all, and a consumer had to infer their standing from a warning.
- An adapter that annotates existing nodes still reports its own health.
  `LAYOUT` here is `INCOMPLETE` with a bounded detail code, which says bounds
  were measured and occlusion was not probed. A caller that requires an
  unobscured target must refuse rather than read the missing probe as a clear
  one.
- The two adapters add evidence to a node another adapter produced instead of
  adding nodes of their own: the selected paragraph carries `STATES` evidence
  from the selection adapter and `BOUNDS` evidence from the layout adapter,
  each with its own source locator, extraction rule version, and confidence.
- A selection-scoped snapshot is still a whole envelope. Scope narrows what is
  observed, never what the message has to account for: the frame tree, the
  truncation record, and the redaction summary are all present and all say
  nothing was dropped.
- Bounds are a diagnostic hint, not identity. Nothing in this document may be
  resolved from them, which is why the paragraph also carries a node handle.
