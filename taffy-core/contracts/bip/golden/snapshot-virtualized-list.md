# Golden message: `snapshot-virtualized-list`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-virtualized-list.json`](./snapshot-virtualized-list.json)

What this document proves:

- Only the rows that currently exist are present. The list reports its real size, each row reports its position, and the difference is carried by an explicit truncation report.
- A recycled row is a new node with a new identifier. Node identifiers are never reused inside a page epoch, so offscreen list continuity can never be inferred from a reused element.
- The `VIRTUALIZED_CONTENT_PARTIAL` warning tells a consumer that scrolling would produce different nodes, not more of these nodes.
