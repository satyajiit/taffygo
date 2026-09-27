# Golden message: `snapshot-request-document-scope`

**Message:** `SnapshotRequest` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-request-document-scope.json`](./snapshot-request-document-scope.json)

What this document proves:

- A first-class request: named adapters with an explicit required or optional level, an explicit field list, explicit origin scope, and four separate budgets plus a deadline.
- `expected_page_epoch` is present, so this is a refresh of an existing binding rather than a first bind. A mismatch fails the request instead of returning a different document.
- `task_purpose` is a short trusted label from a local vocabulary, not free text from a page or a model.
