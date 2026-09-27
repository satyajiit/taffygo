# Golden message: `delta-content-signal-change`

**Message:** `PageDelta` from [`delta.schema.json`](../schema/delta.schema.json)  
**Document:** [`delta-content-signal-change.json`](./delta-content-signal-change.json)

What this document proves:

- Text that was concealed at observation became visible afterwards, and the subscriber is told which fields moved: `STATES` and `CONTENT_SIGNALS`. Without `CONTENT_SIGNALS` in the `SemanticField` vocabulary this change could only have been reported as "something about this node changed", which is the difference between a subscriber that can re-evaluate and one that can only re-fetch.
- The run keeps `IMPERATIVE_INSTRUCTION_SHAPE` after `HIDDEN_BY_STYLE` and `ZERO_WIDTH_CHARACTERS` stop applying. A signal that no longer holds is removed; the ones that still hold stay. Visibility is not trust, and becoming visible does not make a run first-party.
- The content trust label does not move. Who wrote a run is not a function of how it is presented, so a presentation change is never a trust change.
