# Golden message: `delta-node-change`

**Message:** `PageDelta` from [`delta.schema.json`](../schema/delta.schema.json)  
**Document:** [`delta-node-change.json`](./delta-node-change.json)

What this document proves:

- An incremental update that only applies when the epoch, the from-revision, and the sequence match what the subscriber already holds.
- A changed node arrives in full alongside the explicit list of fields that changed, so a subscriber can decide whether the change matters without diffing text.
- A removed identifier is retired, not recycled, and the coalesced mutation count says how many source mutations the adapter folded into this one message.
