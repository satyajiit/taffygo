# Golden message: `snapshot-truncated`

**Message:** `PageSnapshot` from [`snapshot.schema.json`](../schema/snapshot.schema.json)  
**Document:** [`snapshot-truncated.json`](./snapshot-truncated.json)

What this document proves:

- Truncation is stated, not implied: `truncated` is true, both budgets that were reached are named in the order they were hit, and the omitted node and byte counts are exact.
- `may_change_answer` is true. A consumer that reads this snapshot may not present its result as complete.
- The list node still reports the real list size in an allowlisted attribute, so a task can tell how much of the page it did not see.
