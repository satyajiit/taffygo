# Golden message: `backpressure-notice`

**Message:** `BackpressureNotice` from [`delta.schema.json`](../schema/delta.schema.json)  
**Document:** [`backpressure-notice.json`](./backpressure-notice.json)

What this document proves:

- A stream that cannot keep up says so instead of quietly dropping updates, which would leave a subscriber acting on a stale projection.
- The dropped categories are text and layout only. Lifecycle and node-removal signals are dropped last precisely because losing them is what makes a projection dangerous rather than merely incomplete.
- Because nothing structural was lost, the broker reduced scope instead of demanding a fresh snapshot, and it names the reduced scope explicitly.
