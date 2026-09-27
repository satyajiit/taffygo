# Golden message: `invalidation-cross-document-commit`

**Message:** `PageInvalidation` from [`delta.schema.json`](../schema/delta.schema.json)  
**Document:** [`invalidation-cross-document-commit.json`](./invalidation-cross-document-commit.json)

What this document proves:

- A committed cross-document navigation retires the page epoch outright. Every handle bound to it is dead and no refresh revives it.
- The replacement epoch is named, and it is a fresh binding rather than a continuation: no node identifier from the retired epoch is valid under it.
- `resnapshot_required` is true and the invalidation is scoped to the whole page rather than to child frames.
