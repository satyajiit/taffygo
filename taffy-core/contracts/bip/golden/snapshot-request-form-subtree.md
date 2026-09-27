# Golden message: `snapshot-request-form-subtree`

**Message:** `SnapshotRequest` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-request-form-subtree.json`](./snapshot-request-form-subtree.json)

What this document proves:

- `SECTION` is not an unnamed slice of a document. It carries one
  `form_root`, whose semantic node and graph floor were issued by the browser.
- The form adapter is required. A renderer without live form semantics refuses
  the request instead of returning a broader DOM observation.
- The request is read-only evidence. Nothing in the root, adapters, or fields
  authorizes filling a control, submitting the form, or clicking a page node.
- Child frames are excluded. A form root belongs to the one addressed document
  and cannot become an implicit cross-frame read.
