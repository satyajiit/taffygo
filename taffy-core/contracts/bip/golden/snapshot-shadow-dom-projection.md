# Golden message: `snapshot-shadow-dom-projection`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-shadow-dom-projection.json`](./snapshot-shadow-dom-projection.json)

What this document proves:

- Open, user-visible shadow content is projected as ordinary nodes and relationships. No implementation-specific tree pointer, host reference, or internal shadow path appears anywhere in the message.
- The evidence for the projected nodes names the accessibility and form-control adapters, and the transformation is marked, so the projection is never mistaken for a plain document fact.
- A closed shadow root is reported as not projected rather than silently skipped. Closed content is reached only through capabilities Chromium already has, never through a bypass added for the assistant.
